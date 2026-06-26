#!/usr/bin/env python3
"""
실제 시간제한(기본 10초) 환경에서 nnue_final vs bot_strong 를 N판 대국시켜
승률과 timeout(몰수) 발생을 집계하는 게이트 벤치마크.

- 매판 새 랜덤 맵, 선후공 스왑.
- strict_time=True: 총시간 초과 시 몰수 → timeout이 패배로 정직하게 반영.
- 시간 모드는 CPU 경합이 측정을 왜곡하므로 병렬도를 낮게(기본 4) 둔다.

사용:
  python bench.py --n 1000 --time 10000 --workers 4
  python bench.py --n 40 --time 10000 --workers 4      # 타임아웃 점검용 소규모
"""
import argparse
import concurrent.futures as cf
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import referee


def play(t):
    idx, botcmd, strongcmd, total_ms, grace_ms, seed, fixed = t
    rng = random.Random(seed)
    rows = referee.random_board(rng)
    a_first = (idx % 2 == 0)
    if a_first:
        fc, sc, nnue_side = botcmd, strongcmd, 1
    else:
        fc, sc, nnue_side = strongcmd, botcmd, 2
    # fixed>0: 고정깊이(빠름, 대규모 테스트용, 시간무제한). 아니면 실시간 strict.
    strict = (fixed == 0)
    tms = total_ms if fixed == 0 else 1_000_000
    res = referee.play_game(fc, sc, rows, total_ms=tms, grace_ms=grace_ms,
                            strict_time=strict, verbose=False)
    winner = res["winner"]          # 1 first, 2 second, 0 draw
    reason = res.get("reason", "")
    forfeit = "몰수" in reason
    nnue_timeout = forfeit and (winner != nnue_side) and (winner != 0)
    if winner == 0:
        r = 0
    elif winner == nnue_side:
        r = 1
    else:
        r = -1
    return r, nnue_timeout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bot", default="./nnue_final.exe")
    ap.add_argument("--strong", default="./bot_strong.exe")
    ap.add_argument("--n", type=int, default=1000)
    ap.add_argument("--time", type=int, default=10000, help="각 봇 총 사고시간(ms)")
    ap.add_argument("--grace", type=int, default=1000)
    ap.add_argument("--workers", type=int, default=4, help="동시 게임 수(시간 모드는 낮게)")
    ap.add_argument("--depth", type=int, default=0, help=">0이면 고정깊이(대규모 테스트, 워커↑ 가능)")
    ap.add_argument("--seed", type=int, default=5_000_000)
    args = ap.parse_args()

    if args.depth > 0:
        os.environ["FIXED_DEPTH"] = str(args.depth)   # 고정깊이 대규모 모드
    else:
        os.environ.pop("FIXED_DEPTH", None)            # 실시간 strict 모드

    botcmd = args.bot.split()
    strongcmd = args.strong.split()
    tasks = [(i, botcmd, strongcmd, args.time, args.grace, args.seed + i, args.depth)
             for i in range(args.n)]

    w = d = l = to = 0
    done = 0
    t0 = time.time()
    with cf.ThreadPoolExecutor(max_workers=args.workers) as pool:
        for r, nt in pool.map(play, tasks):
            if r == 1:
                w += 1
            elif r == 0:
                d += 1
            else:
                l += 1
            if nt:
                to += 1
            done += 1
            if done % max(50, args.n // 20) == 0 or done == args.n:
                dec = w + l
                wr = w / dec * 100 if dec else 0
                print(f"  {done}/{args.n}  승{w} 무{d} 패{l}  "
                      f"승률(결정){wr:.1f}% 승률(전체){w/done*100:.1f}%  "
                      f"타임아웃{to}  ({time.time()-t0:.0f}s)")
                sys.stdout.flush()

    dec = w + l
    print("\n==== 최종 ====")
    print(f"총 {args.n}판: {w}승 {d}무 {l}패  | nnue 타임아웃 {to}판")
    print(f"승률(결정승): {w/dec*100:.1f}%   승률(전체): {w/args.n*100:.1f}%")


if __name__ == "__main__":
    main()
