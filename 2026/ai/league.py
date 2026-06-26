#!/usr/bin/env python3
"""
4봇 풀리그(round-robin) 자동 대국 + 상세 로그.

- 타겟 봇: 18913 / 19405 / 19931 / 19941  (ai/<id>.exe)
- 보드 b마다 랜덤 10x17 보드(1~9)를 1개 생성, 그 보드를 해당 보드의 모든 쌍/좌석이 공유.
- 각 쌍(4C2=6) × 각 보드 → 2게임: (i 선공, j 후공), (j 선공, i 후공)  같은 보드.
- referee.play_game / referee.random_board 재사용 (strict_time=True 고정).

집계: 봇별 승/무/패/승점(승2 무1 패0), 결정승률, 헤드투헤드 4x4, 선/후공 승수,
      몰수(타임아웃) 횟수, 평균 점령 칸 수.

로그: ai/logs/<오늘날짜>/ 에 저장
  - summary.txt    : 날짜·설정·순위표·헤드투헤드·좌석/몰수 요약
  - boardNN.txt    : 그 보드의 10x17 숫자판 + 해당 보드에서 벌어진 12게임의
                     참가 봇, 수순(진행 과정, 매 수 점수 포함), 결과·승자

사용:
  py ai/league.py --boards 20 --time 10000 --workers 4 --seed 20260626
"""
import argparse
import concurrent.futures as cf
import datetime
import itertools
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import referee

BOTS = ["18913", "19405", "19931", "19941"]
HERE = os.path.dirname(os.path.abspath(__file__))


def bot_cmd(name):
    return [os.path.join(HERE, f"{name}.exe")]


def board_rows(seed):
    """seed로 결정되는 보드를 재현(저장/실행 모두 동일)."""
    return referee.random_board(random.Random(seed))


def play_one(t):
    """한 게임. (board_idx, first, second, seed, time_ms, grace_ms)"""
    bidx, first, second, seed, time_ms, grace_ms = t
    rows = board_rows(seed)
    os.environ.pop("FIXED_DEPTH", None)
    res = referee.play_game(
        bot_cmd(first), bot_cmd(second), rows,
        total_ms=time_ms, grace_ms=grace_ms,
        strict_time=True, verbose=False,
    )
    return bidx, first, second, res


def format_game_log(rows, first, second, res):
    """한 게임을 심판 프로토콜 스타일로. 맨 앞에 승자, 이어서 INIT/수순/점수.

      WINNER FIRST <botid>            (또는 SECOND <botid> / DRAW)
      INIT <17자리 x 10줄>
      FIRST  r1 c1 r2 c2 <걸린ms>     (수마다 점령한 사각형 + 소요시간)
      SECOND r1 c1 r2 c2 <걸린ms>
      ...                             (PASS = -1 -1 -1 -1)
      FINISH
      SCOREFIRST  <선공 점수>
      SCORESECOND <후공 점수>
    """
    fs, ss = res["first_score"], res["second_score"]
    w = res["winner"]
    reason = res.get("reason", "")
    moves = res.get("moves", [])

    if w == 0:
        winner_line = "WINNER DRAW"
    elif w == 1:
        winner_line = f"WINNER FIRST {first}"
    else:
        winner_line = f"WINNER SECOND {second}"
    if "몰수" in reason:          # 비정상 종료는 사유를 함께 남김
        winner_line += f"   ({reason})"

    lines = [winner_line, "INIT " + " ".join(rows)]
    for m in moves:
        turn, mv = m[0], m[1]
        ms = m[2] if len(m) > 2 else 0
        seat = "FIRST" if turn == 1 else "SECOND"
        r1, c1, r2, c2 = mv
        lines.append(f"{seat} {r1} {c1} {r2} {c2} {ms}")
    lines.append("FINISH")
    lines.append(f"SCOREFIRST {fs}")
    lines.append(f"SCORESECOND {ss}")
    return "\n".join(lines)


def write_game_folder(logdir, seq, bidx, seed, first, second, res):
    """게임 하나를 전용 폴더에 즉시 저장. 폴더명: g<순번>_b<보드>_<선>-vs-<후>"""
    rows = board_rows(seed)
    name = f"g{seq:03d}_b{bidx:02d}_{first}-vs-{second}"
    gdir = os.path.join(logdir, name)
    os.makedirs(gdir, exist_ok=True)
    with open(os.path.join(gdir, "game.txt"), "w", encoding="utf-8") as f:
        f.write(format_game_log(rows, first, second, res) + "\n")


def main():
    ap = argparse.ArgumentParser(description="4봇 풀리그 대국 + 상세 로그")
    ap.add_argument("--boards", type=int, default=20, help="보드 수 (총게임 = boards*6*2)")
    ap.add_argument("--time", type=int, default=10000, help="각 봇 총 사고시간(ms)")
    ap.add_argument("--grace", type=int, default=1500, help="한 수 하드 타임아웃 여유(ms)")
    ap.add_argument("--workers", type=int, default=4, help="동시 게임 수(시간 모드는 낮게)")
    ap.add_argument("--seed", type=int, default=20260626, help="랜덤 보드 시드 베이스")
    args = ap.parse_args()

    pairs = list(itertools.combinations(BOTS, 2))  # 6쌍
    tasks = []
    for b in range(args.boards):
        bseed = args.seed + b
        for i, j in pairs:
            tasks.append((b, i, j, bseed, args.time, args.grace))
            tasks.append((b, j, i, bseed, args.time, args.grace))
    total = len(tasks)

    # 집계 구조
    wins = {n: 0 for n in BOTS}
    losses = {n: 0 for n in BOTS}
    draws = {n: 0 for n in BOTS}
    pts = {n: 0 for n in BOTS}
    first_wins = {n: 0 for n in BOTS}
    second_wins = {n: 0 for n in BOTS}
    forfeit_loss = {n: 0 for n in BOTS}
    cells_for = {n: 0 for n in BOTS}
    cells_games = {n: 0 for n in BOTS}
    h2h = {a: {b: 0 for b in BOTS} for a in BOTS}

    # 보드별 게임 결과 수집: results[bidx] = list of (first, second, res)
    results = {b: [] for b in range(args.boards)}

    def record(first, second, res):
        fs, ss = res["first_score"], res["second_score"]
        w = res["winner"]
        forfeit = "몰수" in res.get("reason", "")
        cells_for[first] += fs
        cells_for[second] += ss
        cells_games[first] += 1
        cells_games[second] += 1
        if w == 0:
            draws[first] += 1; draws[second] += 1
            pts[first] += 1; pts[second] += 1
        elif w == 1:
            wins[first] += 1; losses[second] += 1
            pts[first] += 2; first_wins[first] += 1
            h2h[first][second] += 1
            if forfeit:
                forfeit_loss[second] += 1
        else:
            wins[second] += 1; losses[first] += 1
            pts[second] += 2; second_wins[second] += 1
            h2h[second][first] += 1
            if forfeit:
                forfeit_loss[first] += 1

    today = datetime.date.today().isoformat()
    logdir = os.path.join(HERE, "logs", today)
    os.makedirs(logdir, exist_ok=True)

    print(f"리그 시작: 봇 {BOTS}, 보드 {args.boards}개, 총 {total}게임, "
          f"시간 {args.time}ms strict, 워커 {args.workers}")
    print(f"로그 폴더: {logdir}  (게임마다 개별 폴더에 즉시 저장)")
    sys.stdout.flush()

    t0 = time.time()
    done = 0
    step = max(10, total // 20)
    with cf.ThreadPoolExecutor(max_workers=args.workers) as pool:
        for bidx, first, second, res in pool.map(play_one, tasks):
            record(first, second, res)
            results[bidx].append((first, second, res))
            done += 1
            # 게임이 끝나는 즉시 그 게임 전용 폴더에 저장 (한번에 모아 저장하지 않음)
            write_game_folder(logdir, done, bidx, args.seed + bidx, first, second, res)
            if done % step == 0 or done == total:
                print(f"  진행 {done}/{total}  ({time.time()-t0:.0f}s)")
                sys.stdout.flush()
    elapsed = time.time() - t0

    # ---------- 콘솔/요약 텍스트 ----------
    out = []
    out.append("=" * 64)
    out.append(f"4봇 풀리그 결과")
    out.append(f"날짜      : {today}")
    out.append(f"참가 봇   : {', '.join(BOTS)}  (ai/<id>.exe)")
    out.append(f"진행 방식 : 풀리그(round-robin). 보드 {args.boards}개, 각 쌍이")
    out.append(f"            동일 보드에서 선/후공 1게임씩 → 총 {total}게임")
    out.append(f"시간 제한 : 각 봇 {args.time}ms (초과 시 몰수패), 소요 {elapsed:.0f}s")
    out.append(f"보드 시드 : base={args.seed} (보드 b = random_board(seed+b))")
    out.append("=" * 64)
    out.append("")
    out.append("[순위표]  승점 = 승2·무1·패0")
    out.append(f"{'순위':<4}{'봇':<8}{'승점':>5}{'승':>5}{'무':>5}{'패':>5}"
               f"{'결정승률':>10}{'평균칸':>8}")
    order = sorted(BOTS, key=lambda n: (pts[n], wins[n]), reverse=True)
    for rank, n in enumerate(order, 1):
        dec = wins[n] + losses[n]
        wr = wins[n] / dec * 100 if dec else 0.0
        avg = cells_for[n] / cells_games[n] if cells_games[n] else 0.0
        out.append(f"{rank:<4}{n:<8}{pts[n]:>5}{wins[n]:>5}{draws[n]:>5}{losses[n]:>5}"
                   f"{wr:>9.1f}%{avg:>8.1f}")
    out.append("")
    out.append("[헤드투헤드]  행 봇이 열 봇 상대로 거둔 승수")
    out.append("        " + "".join(f"{c:>8}" for c in BOTS))
    for a in BOTS:
        row = f"{a:<8}"
        for b in BOTS:
            row += ("       -" if a == b else f"{h2h[a][b]:>8}")
        out.append(row)
    out.append("")
    out.append("[좌석별 승수 / 몰수패]")
    out.append(f"{'봇':<8}{'선공승':>8}{'후공승':>8}{'몰수패':>8}")
    for n in BOTS:
        out.append(f"{n:<8}{first_wins[n]:>8}{second_wins[n]:>8}{forfeit_loss[n]:>8}")
    out.append("")
    champ = order[0]
    out.append(f">>> 우승: {champ}  (승점 {pts[champ]}, {wins[champ]}승 "
               f"{draws[champ]}무 {losses[champ]}패)")
    summary = "\n".join(out)
    print("\n" + summary)

    # ---------- 요약(집계)만 마지막에 기록. 게임 로그는 진행 중 이미 저장됨 ----------
    with open(os.path.join(logdir, "summary.txt"), "w", encoding="utf-8") as f:
        f.write(summary + "\n")

    print(f"\n로그 저장 완료: {logdir}")
    print(f"  - summary.txt          (순위표·헤드투헤드·우승)")
    print(f"  - g001_... ~ g{total:03d}_...  (게임마다 개별 폴더, 진행 중 즉시 저장)")


if __name__ == "__main__":
    main()
