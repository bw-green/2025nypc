#!/usr/bin/env python3
"""
두 개의 봇 실행파일을 서로 대국시키는 referee(심판).

게임 규칙 (봇의 getCandidates 와 동일):
  - 보드 10행 x 17열, 각 칸은 숫자값(살아있는 칸만 합산에 기여)
  - 직사각형을 골라 그 안의 '살아있는' 칸 값의 합이 정확히 10이어야 함
  - 직사각형의 위/아래/왼/오른쪽 테두리 각각에 살아있는 칸이 최소 1개씩 있어야 함
  - 점령 시 직사각형 안 모든 칸을 자기 소유로(상대 칸이면 탈취), 모두 dead 처리
  - PASS = "-1 -1 -1 -1", 두 명이 연속으로 PASS 하면 게임 종료
  - 종료 시 소유 칸 수가 많은 쪽 승

프로토콜:
  심판 -> 봇 : "READY FIRST" 또는 "READY SECOND"   ;  봇 -> 심판 : "OK"
  심판 -> 봇 : "INIT" + 10줄 보드
  심판 -> 봇 : "TIME <내남은ms> <상대남은ms>"        ;  봇 -> 심판 : "r1 c1 r2 c2"
  심판 -> 봇 : "OPP r1 c1 r2 c2 <소요ms>"            (상대 수 통보, 응답 없음)
  심판 -> 봇 : "FINISH"

사용 예:
  python3 referee.py ./botA ./botB                  # 1판
  python3 referee.py ./botA ./botB -n 20 --swap     # 20판, 매판 선후공 교대
  python3 referee.py ./botA ./botB -n 50 --swap -t 5000   # 각자 총 5초
  python3 referee.py ./botA ./botB --board board.txt -v    # 고정 보드 + 수순 출력
"""

import argparse
import os
import queue
import random
import subprocess
import sys
import threading
import time

R, C = 10, 17
PASS = (-1, -1, -1, -1)


# ---------- 보드 ----------
def random_board(rng):
    # 1~9 숫자. 합 10 직사각형이 생기도록 작은 값도 섞임.
    return ["".join(str(rng.randint(1, 9)) for _ in range(C)) for _ in range(R)]


def load_board(path):
    with open(path) as f:
        rows = [ln.strip() for ln in f if ln.strip()]
    rows = rows[:R]
    assert len(rows) == R and all(len(x) == C for x in rows), "보드는 10x17 이어야 함"
    return rows


# ---------- 게임 상태 ----------
class State:
    def __init__(self, rows):
        self.val = [[int(rows[r][c]) for c in range(C)] for r in range(R)]
        self.alive = [[True] * C for _ in range(R)]
        self.owner = [[0] * C for _ in range(R)]  # 0 none, 1 FIRST, 2 SECOND

    def legal(self, mv, player):
        r1, c1, r2, c2 = mv
        if not (0 <= r1 <= r2 < R and 0 <= c1 <= c2 < C):
            return False, "범위 밖"
        s = 0
        for r in range(r1, r2 + 1):
            for c in range(c1, c2 + 1):
                if self.alive[r][c]:
                    s += self.val[r][c]
        if s != 10:
            return False, f"합={s}"
        top = any(self.alive[r1][c] for c in range(c1, c2 + 1))
        bot = any(self.alive[r2][c] for c in range(c1, c2 + 1))
        lft = any(self.alive[r][c1] for r in range(r1, r2 + 1))
        rgt = any(self.alive[r][c2] for r in range(r1, r2 + 1))
        if not (top and bot and lft and rgt):
            return False, "테두리에 살아있는 칸 없음"
        return True, ""

    def apply(self, mv, player):
        r1, c1, r2, c2 = mv
        for r in range(r1, r2 + 1):
            for c in range(c1, c2 + 1):
                self.alive[r][c] = False
                self.owner[r][c] = player

    def has_legal(self, player):
        # 합10 직사각형이 하나라도 존재하는지 (2차원 누적합으로 가속)
        ps = [[0] * (C + 1) for _ in range(R + 1)]
        for r in range(R):
            for c in range(C):
                v = self.val[r][c] if self.alive[r][c] else 0
                ps[r + 1][c + 1] = ps[r][c + 1] + ps[r + 1][c] - ps[r][c] + v
        pa = [[0] * (C + 1) for _ in range(R + 1)]
        for r in range(R):
            for c in range(C):
                a = 1 if self.alive[r][c] else 0
                pa[r + 1][c + 1] = pa[r][c + 1] + pa[r + 1][c] - pa[r][c] + a

        def rect(p, r1, c1, r2, c2):
            return p[r2 + 1][c2 + 1] - p[r1][c2 + 1] - p[r2 + 1][c1] + p[r1][c1]

        for r1 in range(R):
            for r2 in range(r1, R):
                for c1 in range(C):
                    for c2 in range(c1, C):
                        if rect(ps, r1, c1, r2, c2) != 10:
                            continue
                        if rect(pa, r1, c1, r1, c2) == 0:
                            continue
                        if rect(pa, r2, c1, r2, c2) == 0:
                            continue
                        if rect(pa, r1, c1, r2, c1) == 0:
                            continue
                        if rect(pa, r1, c2, r2, c2) == 0:
                            continue
                        return True
        return False

    def score(self):
        a = sum(row.count(1) for row in self.owner)
        b = sum(row.count(2) for row in self.owner)
        return a, b


# ---------- 봇 프로세스 래퍼 ----------
class Bot:
    def __init__(self, cmd, name, env=None):
        self.name = name
        run_env = None
        if env:
            run_env = os.environ.copy()
            run_env.update(env)
        self.proc = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            bufsize=1,
            env=run_env,
        )
        # Windows에는 파이프용 select가 없어 백그라운드 reader 스레드로 한 줄씩 큐에 적재.
        self._q = queue.Queue()
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()

    def _read_loop(self):
        try:
            for line in self.proc.stdout:
                self._q.put(line)
        except Exception:
            pass
        finally:
            self._q.put(None)  # EOF 표시

    def send(self, line):
        try:
            self.proc.stdin.write(line + "\n")
            self.proc.stdin.flush()
        except (OSError, ValueError):
            pass  # 상대가 이미 종료된 경우 등은 무시

    def readline(self, timeout):
        """timeout 초 안에 한 줄 읽기. 실패 시 None."""
        try:
            line = self._q.get(timeout=timeout)
        except queue.Empty:
            return None
        if line is None:
            return None
        return line.strip()

    def close(self):
        try:
            self.send("FINISH")
        except Exception:
            pass
        try:
            self.proc.wait(timeout=2)
        except Exception:
            self.proc.kill()
        for s in (self.proc.stdin, self.proc.stdout):
            try:
                s.close()
            except Exception:
                pass


# ---------- 한 판 ----------
def play_game(cmd_first, cmd_second, rows, total_ms, grace_ms,
              strict_time, verbose):
    """
    cmd_first / cmd_second: 실행 명령 리스트
    return: dict(first_score, second_score, winner, reason, moves)
    winner: 1=first, 2=second, 0=draw
    """
    st = State(rows)
    bots = {1: Bot(cmd_first, "FIRST"), 2: Bot(cmd_second, "SECOND")}
    bank = {1: total_ms, 2: total_ms}  # 남은 시간(ms)
    init_block = "INIT\n" + "\n".join(rows)

    try:
        # READY
        for who, label in ((1, "FIRST"), (2, "SECOND")):
            bots[who].send(f"READY {label}")
            ok = bots[who].readline(timeout=10)
            if ok != "OK":
                return _forfeit(st, bots, who, f"READY 응답 이상('{ok}')")

        # INIT
        for who in (1, 2):
            bots[who].send(init_block)

        moves = []
        passes = 0
        turn = 1
        cap_turns = 4 * R * C  # 안전장치

        while passes < 2 and cap_turns > 0:
            cap_turns -= 1
            other = 3 - turn
            me = bots[turn]

            me.send(f"TIME {max(0, int(bank[turn]))} {max(0, int(bank[other]))}")
            hard = bank[turn] / 1000.0 + grace_ms / 1000.0  # 초

            t0 = time.perf_counter()
            line = me.readline(timeout=max(hard, 0.5))
            elapsed_ms = (time.perf_counter() - t0) * 1000.0

            if line is None:
                return _forfeit(st, bots, turn, "시간초과/무응답")

            try:
                mv = tuple(int(x) for x in line.split()[:4])
                assert len(mv) == 4
            except Exception:
                return _forfeit(st, bots, turn, f"출력 파싱 실패('{line}')")

            bank[turn] -= elapsed_ms
            over = bank[turn] < 0
            if over and strict_time:
                return _forfeit(st, bots, turn, f"총시간 초과({-bank[turn]:.0f}ms)")

            if mv == PASS:
                passes += 1
                if verbose:
                    a, b = st.score()
                    print(f"  {'FIRST' if turn==1 else 'SECOND':>6}  PASS"
                          f"            {elapsed_ms:6.0f}ms  남은{bank[turn]:6.0f}  "
                          f"점수 {a}-{b}")
            else:
                ok, why = st.legal(mv, turn)
                if not ok:
                    return _forfeit(st, bots, turn,
                                    f"반칙수 {mv}: {why}")
                st.apply(mv, turn)
                passes = 0
                if verbose:
                    a, b = st.score()
                    warn = "  [시간초과]" if over else ""
                    print(f"  {'FIRST' if turn==1 else 'SECOND':>6}  "
                          f"{mv[0]},{mv[1]}-{mv[2]},{mv[3]:<2}".ljust(20)
                          + f"{elapsed_ms:6.0f}ms  남은{bank[turn]:6.0f}  "
                          f"점수 {a}-{b}{warn}")

            # 상대에게 통보
            bots[other].send(
                f"OPP {mv[0]} {mv[1]} {mv[2]} {mv[3]} {int(elapsed_ms)}"
            )
            moves.append((turn, mv, int(elapsed_ms)))
            turn = other

        a, b = st.score()
        winner = 1 if a > b else 2 if b > a else 0
        for w in (1, 2):
            bots[w].close()
        return dict(first_score=a, second_score=b, winner=winner,
                    reason="정상 종료", moves=moves)
    finally:
        for w in (1, 2):
            try:
                bots[w].close()
            except Exception:
                pass


def _forfeit(st, bots, loser, reason):
    a, b = st.score()
    winner = 3 - loser
    for w in (1, 2):
        try:
            bots[w].close()
        except Exception:
            pass
    return dict(first_score=a, second_score=b, winner=winner,
                reason=f"{'FIRST' if loser==1 else 'SECOND'} 몰수패: {reason}",
                moves=[])


# ---------- 메인 ----------
def main():
    ap = argparse.ArgumentParser(description="두 봇 대국 심판")
    ap.add_argument("botA", help="봇A 실행 명령 (예: ./botA)")
    ap.add_argument("botB", help="봇B 실행 명령")
    ap.add_argument("-n", "--games", type=int, default=1, help="대국 수")
    ap.add_argument("--swap", action="store_true",
                    help="매판 선후공 교대 (공정 비교용)")
    ap.add_argument("-t", "--time", type=int, default=10000,
                    help="각 봇 총 사고시간(ms), 기본 10000")
    ap.add_argument("--grace", type=int, default=1500,
                    help="한 수 하드 타임아웃 여유(ms)")
    ap.add_argument("--strict-time", action="store_true",
                    help="총시간 초과 시 즉시 몰수패")
    ap.add_argument("--board", help="고정 보드 파일(10줄)")
    ap.add_argument("--seed", type=int, default=None, help="랜덤 보드 시드")
    ap.add_argument("-v", "--verbose", action="store_true", help="수순 출력")
    args = ap.parse_args()

    cmdA = args.botA.split()
    cmdB = args.botB.split()
    rng = random.Random(args.seed)

    winsA = winsB = draws = 0
    sumA = sumB = 0

    for g in range(args.games):
        rows = load_board(args.board) if args.board else random_board(rng)

        # swap이면 홀수판에서 B가 선공
        a_is_first = (not args.swap) or (g % 2 == 0)
        first_cmd, second_cmd = (cmdA, cmdB) if a_is_first else (cmdB, cmdA)
        first_name, second_name = ("A", "B") if a_is_first else ("B", "A")

        if args.verbose:
            print(f"\n=== Game {g+1}: FIRST={first_name}  SECOND={second_name} ===")

        res = play_game(first_cmd, second_cmd, rows,
                        args.time, args.grace, args.strict_time, args.verbose)

        # 결과를 A/B 기준으로 환산
        fs, ss = res["first_score"], res["second_score"]
        if a_is_first:
            scoreA, scoreB = fs, ss
        else:
            scoreA, scoreB = ss, fs
        sumA += scoreA
        sumB += scoreB

        if res["winner"] == 0:
            draws += 1
            wtag = "무승부"
        else:
            winner_ab = first_name if res["winner"] == 1 else second_name
            if winner_ab == "A":
                winsA += 1
            else:
                winsB += 1
            wtag = f"{winner_ab} 승"

        print(f"Game {g+1:>3}: A {scoreA:>2} - {scoreB:<2} B   "
              f"[{wtag}]  ({res['reason']})")

    print("\n========== 최종 ==========")
    print(f"A: {winsA}승  B: {winsB}승  무: {draws}")
    print(f"평균 점수  A={sumA/args.games:.1f}  B={sumB/args.games:.1f}")
    if winsA + winsB > 0:
        print(f"A 승률(무승부 제외): {winsA/(winsA+winsB)*100:.1f}%")


if __name__ == "__main__":
    main()
