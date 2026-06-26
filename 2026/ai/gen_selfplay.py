#!/usr/bin/env python3
"""
랜덤 보드 100개 생성 + 각 보드를 강한 봇 self-play로 플레이해 '스테이지별 데이터' 저장.

저장물 (selfplay/ 디렉토리):
  boards.txt              : 100개 초기 보드(각 10줄 x 17숫자, 빈 줄로 구분)
  game_000.txt ...        : 게임별 로그
      줄1: BOARD
      다음 10줄: 초기 숫자판
      이후 각 수마다:
        STEP <idx> P<player> MOVE r1 c1 r2 c2   (PASS면 -1 -1 -1 -1)
        OWN <10줄, 각 17문자: 0=빈/생존, 1=FIRST소유(죽음), 2=SECOND소유(죽음)>  ← 그 수를 둔 직후 상태
      마지막: RESULT first=<a> second=<b> winner=<1/2/0>

  ※ 값(숫자)은 고정이므로 OWN 격자 + 초기판이면 완전한 상태 복원 가능(생존=OWN==0).
  ※ '완벽'은 exact 풀이가 아니라 깊은 탐색(FIXED_DEPTH) self-play의 '강한 플레이'임.

사용:
  python gen_selfplay.py --n 100 --bot "./bot_mlp_q.exe data.bin" --depth 6
"""
import argparse, concurrent.futures as cf, os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import referee

R, C = 10, 17


def owner_grid_str(st):
    return "\n".join("".join(str(st.owner[r][c]) for c in range(C)) for r in range(R))


def play_one(t):
    i, cmd, rows = t
    res = referee.play_game(cmd, cmd, rows, total_ms=1_000_000, grace_ms=5000,
                            strict_time=False, verbose=False)
    return i, res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=100)
    ap.add_argument("--bot", default="./bot_mlp_q.exe data.bin", help="self-play 봇(양쪽 동일)")
    ap.add_argument("--depth", type=int, default=6, help="고정 탐색 깊이(강할수록 완벽에 근접, 느림)")
    ap.add_argument("--out", default="selfplay")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--workers", type=int, default=16)
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    os.environ["FIXED_DEPTH"] = str(args.depth)
    cmd = args.bot.split()

    # 보드 생성 + boards.txt
    boards = []
    with open(os.path.join(args.out, "boards.txt"), "w") as fb:
        for i in range(args.n):
            rows = referee.random_board(random.Random(args.seed * 1000000 + i))
            boards.append(rows)
            fb.write("\n".join(rows) + "\n\n")

    # 병렬 self-play (고정깊이라 타이밍 무관 → 16워커 OK)
    tasks = [(i, cmd, boards[i]) for i in range(args.n)]
    done = 0
    with cf.ThreadPoolExecutor(max_workers=args.workers) as pool:
        for i, res in pool.map(play_one, tasks):
            rows = boards[i]
            st = referee.State(rows)
            with open(os.path.join(args.out, f"game_{i:03d}.txt"), "w") as g:
                g.write("BOARD\n" + "\n".join(rows) + "\n")
                for idx, (turn, mv) in enumerate(res["moves"]):
                    if mv != referee.PASS:
                        st.apply(mv, turn)
                    g.write(f"STEP {idx} P{turn} MOVE {mv[0]} {mv[1]} {mv[2]} {mv[3]}\n")
                    g.write("OWN\n" + owner_grid_str(st) + "\n")
                a, b = st.score()
                g.write(f"RESULT first={a} second={b} winner={res['winner']}\n")
            done += 1
            if done % 10 == 0:
                print(f"  {done}/{args.n} done"); sys.stdout.flush()
    print(f"# 완료 → {args.out}/ (boards.txt + game_000..{args.n-1:03d}.txt)")


if __name__ == "__main__":
    main()
