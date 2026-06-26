#!/usr/bin/env python3
"""
referee.py 시뮬레이터 위에서 self-play로 bot_param 의 eval 가중치를 학습.

방법: (1+lambda) 진화전략(self-play RL).
  - 챔피언 가중치 theta 유지.
  - 매 세대: theta 주변에 가우시안 섭동으로 challenger lambda 개 생성.
  - 각 challenger 를 챔피언과 self-play(referee, 선후공 스왑) 시켜 승률 측정 = fitness.
  - 가장 잘한 challenger 가 챔피언을 이기면(>승률 임계) 챔피언 교체.
  - sigma(탐색 스텝)는 세대마다 감쇠.
  - 절대 진척 확인용으로 매 세대 baseline(기본 가중치)과도 gauntlet.

경사하강이 아니라 토너먼트 기반이라 미분 불필요하고 잡음에 강함.
게임 평가가 곧 보상이라 시뮬레이터가 그대로 환경 역할을 함.

사용:
  # 먼저 봇 컴파일
  g++ -std=c++17 -O2 -o bot_param bot_param.cpp

  # 빠른 데모 (몇 분)
  python3 train.py --bot ./bot_param --gens 5 --lam 4 --games 4 --depth 3

  # 진짜 학습 (오래 걸림, 백그라운드 권장)
  python3 train.py --bot ./bot_param --gens 200 --lam 8 --games 12 --depth 4 \
                   --out best_weights.txt
"""
import argparse
import os
import random
import sys

# referee.py 를 같은 폴더에서 import
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import referee  # noqa: E402

NW = 6
DEFAULT = [120000.0, 16000.0, 9000.0, -17000.0, -10000.0, 100.0]
# 각 가중치의 섭동 스케일(자릿수 차이 보정). 부호는 자유.
SCALE = [20000.0, 6000.0, 4000.0, 6000.0, 4000.0, 200.0]


def write_w(path, w):
    with open(path, "w") as f:
        f.write(" ".join(f"{x:.4f}" for x in w))


def match(bot, wfA, wfB, games, total_ms, rng):
    """A(wfA) vs B(wfB) self-play, 선후공 스왑. A의 승수를 반환."""
    winsA = 0
    played = 0
    for g in range(games):
        rows = referee.random_board(rng)
        a_first = (g % 2 == 0)
        if a_first:
            fc, sc = [bot, wfA], [bot, wfB]
        else:
            fc, sc = [bot, wfB], [bot, wfA]
        res = referee.play_game(fc, sc, rows,
                                total_ms=total_ms, grace_ms=2000,
                                strict_time=False, verbose=False)
        if res["winner"] == 0:
            continue
        winner_is_first = (res["winner"] == 1)
        a_won = (winner_is_first == a_first)
        winsA += 1 if a_won else 0
        played += 1
    return winsA, played


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bot", required=True, help="bot_param 실행파일 경로")
    ap.add_argument("--gens", type=int, default=50, help="세대 수")
    ap.add_argument("--lam", type=int, default=8, help="세대당 challenger 수")
    ap.add_argument("--games", type=int, default=10, help="평가 게임 수(스왑 포함)")
    ap.add_argument("--depth", type=int, default=4, help="학습 중 고정 탐색 깊이(빠름)")
    ap.add_argument("--total-ms", type=int, default=100000,
                    help="referee 총시간(고정깊이면 사실상 무제한이면 됨)")
    ap.add_argument("--sigma", type=float, default=1.0, help="초기 섭동 배율")
    ap.add_argument("--decay", type=float, default=0.97, help="sigma 감쇠율/세대")
    ap.add_argument("--accept", type=float, default=0.55,
                    help="챔피언 교체 승률 임계")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--out", default="best_weights.txt")
    ap.add_argument("--init", default=None,
                    help="시작 가중치 파일(없으면 기본값)")
    args = ap.parse_args()

    # 학습 게임을 빠르고 재현가능하게: 고정 깊이 모드
    os.environ["FIXED_DEPTH"] = str(args.depth)

    rng = random.Random(args.seed)
    os.makedirs("_w", exist_ok=True)

    # 챔피언 초기화
    if args.init and os.path.exists(args.init):
        theta = [float(x) for x in open(args.init).read().split()][:NW]
        while len(theta) < NW:
            theta.append(DEFAULT[len(theta)])
    else:
        theta = DEFAULT[:]

    champ_f = "_w/champ.txt"
    base_f = "_w/base.txt"
    write_w(champ_f, theta)
    write_w(base_f, DEFAULT)

    sigma = args.sigma
    print(f"# 학습 시작  gens={args.gens} lam={args.lam} games={args.games} "
          f"depth={args.depth}")
    print(f"# 시작 가중치: {[round(x) for x in theta]}")

    for gen in range(1, args.gens + 1):
        # challenger 생성 + 평가
        best_w, best_rate, best_played = None, -1.0, 0
        for i in range(args.lam):
            cand = [theta[k] + sigma * SCALE[k] * rng.gauss(0, 1)
                    for k in range(NW)]
            cf = f"_w/cand_{i}.txt"
            write_w(cf, cand)
            wins, played = match(args.bot, cf, champ_f,
                                 args.games, args.total_ms, rng)
            rate = wins / played if played else 0.5
            if rate > best_rate:
                best_rate, best_w, best_played = rate, cand, played

        # 챔피언 교체 판정
        replaced = False
        if best_rate >= args.accept and best_w is not None:
            theta = best_w
            write_w(champ_f, theta)
            replaced = True

        # 절대 진척: 현재 챔피언 vs 기본 가중치
        bw, bp = match(args.bot, champ_f, base_f, args.games,
                       args.total_ms, rng)
        base_rate = bw / bp if bp else 0.5

        write_w(args.out, theta)
        print(f"gen {gen:>3}  best_vs_champ={best_rate:.2f}({best_played})  "
              f"{'교체' if replaced else '유지'}  "
              f"champ_vs_base={base_rate:.2f}  sigma={sigma:.3f}  "
              f"w={[round(x) for x in theta]}")
        sys.stdout.flush()
        sigma *= args.decay

    print(f"\n# 완료. 최종 가중치 -> {args.out}")
    print(f"# 사용: ./{os.path.basename(args.bot)} {args.out}")


if __name__ == "__main__":
    main()
