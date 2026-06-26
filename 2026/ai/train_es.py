#!/usr/bin/env python3
"""
자가대국 ES(NES)로 bot_mlp 의 평가망 가중치를 최적화.
  - 목적함수: bot_strong 상대 '평균 점수차(내 칸 - 상대 칸)' 최대화 (모방 아님, 직접 최적화).
  - NES + 대칭(antithetic) 샘플링. 게임은 병렬, FIXED_DEPTH 로 빠르고 재현가능.
  - zero-net 도 이동순서 휴리스틱으로 동작하므로 0에서 출발해도 합리적.

사용:
  g++ -std=c++17 -Ofast -march=native -o bot_mlp.exe bot_mlp.cpp
  g++ -std=c++17 -Ofast -march=native -o bot_strong.exe bot_strong.cpp
  python train_es.py --gens 300 --pop 20 --games 24 --depth 3
출력: mlp.bin (최적 가중치) → ./bot_mlp.exe mlp.bin
"""
import argparse
import concurrent.futures as cf
import os
import random
import struct
import sys
import tempfile
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import referee

IN, H = 12, 28
NP = H*IN + H + H + 1   # ~400


def write_bin(path, theta):
    with open(path, "wb") as f:
        f.write(struct.pack(f"<{NP}f", *theta.astype(np.float32)))


def bot_strong_init():
    """MLP 가중치를 bot_strong 평가(내 feature들의 선형결합)와 동일하게 해석적 초기화.
    ReLU 유닛에 큰 bias를 줘 선형 통과시키고, W2로 bot_strong 계수를 재현. → 동급에서 출발."""
    W1 = np.zeros((H, IN)); b1 = np.zeros(H); W2 = np.zeros(H)
    # forward(f) = bot_strong_eval/1e6 의 계수 (feature 스케일 반영 완료)
    coef = {0: 6.0, 4: 0.28, 6: 0.28, 10: 0.18, 7: -0.46, 9: -0.6, 11: -0.32}
    BIG = 10.0
    for j, (k, c) in enumerate(coef.items()):
        W1[j, k] = 1.0; b1[j] = BIG; W2[j] = c
    b2 = -BIG * sum(coef.values())
    return np.concatenate([W1.flatten(), b1, W2, [b2]]).astype(np.float64)


def play_margin(args):
    """bot_mlp(wfile) vs 임의 상대(opp_cmd: bot_strong 또는 챔피언 bot_mlp) 한 판.
    bot_mlp 관점 점수차 반환. 내 봇=my_depth(MLP_DEPTH), bot_strong 상대=opp_depth(FIXED_DEPTH).
    상대가 bot_mlp(self-play)면 그쪽도 MLP_DEPTH 를 따름 → 대칭."""
    idx, bot, wfile, opp_cmd, my_depth, opp_depth, seed = args
    rng = random.Random(seed)
    rows = referee.random_board(rng)
    a_first = (idx % 2 == 0)
    mlp_cmd = [bot, wfile]
    opp = list(opp_cmd)
    if a_first:
        fc, sc, mlp_side = mlp_cmd, opp, 1
    else:
        fc, sc, mlp_side = opp, mlp_cmd, 2
    os.environ["FIXED_DEPTH"] = str(opp_depth)
    os.environ["MLP_DEPTH"] = str(my_depth)
    res = referee.play_game(fc, sc, rows, total_ms=1_000_000, grace_ms=5000,
                            strict_time=False, verbose=False)
    fs, ss = res["first_score"], res["second_score"]
    return (fs - ss) if mlp_side == 1 else (ss - fs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bot", default="./bot_mlp.exe")
    ap.add_argument("--strong", default="./bot_strong.exe")
    ap.add_argument("--out", default="mlp.bin")
    ap.add_argument("--gens", type=int, default=300)
    ap.add_argument("--pop", type=int, default=20, help="population(짝수, 대칭샘플)")
    ap.add_argument("--games", type=int, default=24, help="후보당 평가 게임 수(스왑)")
    ap.add_argument("--train-depth", type=int, default=5, help="self-play 학습 깊이")
    ap.add_argument("--selfplay", type=int, default=1, help="1=상대를 챔피언(빠름,깊이가능), 0=bot_strong")
    ap.add_argument("--sigma", type=float, default=0.5)
    ap.add_argument("--lr", type=float, default=0.2)
    ap.add_argument("--decay", type=float, default=0.999)
    ap.add_argument("--workers", type=int, default=16)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--init", default=None, help="시작 가중치 bin(없으면 bot_strong 동급)")
    ap.add_argument("--val-every", type=int, default=10, help="N세대마다 theta 직접 검증")
    ap.add_argument("--val-games", type=int, default=60, help="검증 게임 수(고정맵)")
    ap.add_argument("--val-depth", type=int, default=5, help="검증(배치근사) 대칭 깊이")
    args = ap.parse_args()

    rng = np.random.RandomState(args.seed)
    grng = random.Random(args.seed)
    theta = bot_strong_init()   # bot_strong 동급에서 출발
    if args.init and os.path.exists(args.init):
        theta = np.fromfile(args.init, dtype=np.float32).astype(np.float64)[:NP]

    tmp = tempfile.mkdtemp(prefix="es_")
    pool = cf.ThreadPoolExecutor(max_workers=args.workers)
    sigma = args.sigma
    half = args.pop // 2
    best_val = -1.0

    def validate(theta_vec):
        """절대 성능: 대칭 깊이(val_depth)로 bot_strong 과 고정맵 대국 → 승률(%) (무승부 제외)."""
        wf = os.path.join(tmp, "val.bin"); write_bin(wf, theta_vec)
        vd = args.val_depth
        tasks = [(i, args.bot, wf, (args.strong,), vd, vd, 777000 + i)
                 for i in range(args.val_games)]
        res = list(pool.map(play_margin, tasks))
        w = sum(1 for m in res if m > 0); l = sum(1 for m in res if m < 0)
        return (w / (w + l) * 100 if (w + l) else 50.0), float(np.mean(res))

    D = args.train_depth
    # 엘리트 (1+λ) ES + CRN. 상대=과거 챔피언 풀(self-play 드리프트 방지) 또는 bot_strong.
    champ = theta
    pool_files = []
    if args.selfplay:
        p0 = os.path.join(tmp, "pool0.bin"); write_bin(p0, champ); pool_files = [p0]
    print(f"# ES(pool self-play={args.selfplay}) gens={args.gens} "
          f"pop={args.pop} games={args.games} train_depth={D} params={NP}")
    for gen in range(1, args.gens + 1):
        t0 = time.time()
        seeds = [grng.randint(0, 2**31 - 1) for _ in range(args.games)]
        cands = [champ.copy()]
        for _ in range(args.pop):
            cands.append(champ + sigma * rng.randn(NP))
        wfiles = []
        for k, c in enumerate(cands):
            wf = os.path.join(tmp, f"c{k}.bin"); write_bin(wf, c); wfiles.append(wf)
        tasks = []
        for k, wf in enumerate(wfiles):
            for gi, sd in enumerate(seeds):
                if args.selfplay:   # 게임마다 풀에서 상대 순환 → 한 상대 악용(드리프트) 방지
                    opp_cmd = (args.bot, pool_files[gi % len(pool_files)])
                else:
                    opp_cmd = (args.strong,)
                tasks.append((gi, args.bot, wf, opp_cmd, D, D, sd))
        results = list(pool.map(play_margin, tasks))
        per = args.games
        F = np.array([np.mean(results[k*per:(k+1)*per]) for k in range(len(cands))])

        bidx = int(np.argmax(F))
        champ = cands[bidx]          # 챔피언 포함 비교 → 엘리트(단조)
        sigma *= args.decay

        # 풀 갱신: 몇 세대마다 현재 챔프를 상대 풀에 추가(최대 8개, 오래된 것 제거)
        if args.selfplay and gen % 5 == 0:
            pf = os.path.join(tmp, f"pool_g{gen}.bin"); write_bin(pf, champ)
            pool_files.append(pf)
            if len(pool_files) > 8:
                pool_files.pop(0)

        write_bin(args.out, champ)
        valstr = ""
        if gen % args.val_every == 0 or gen == 1:
            vr, vm = validate(champ)
            if vr > best_val:
                best_val = vr
                write_bin(args.out + ".best", champ)
            valstr = f"  [검증 vs_strong 승률={vr:4.1f}% 마진={vm:+.2f} best={best_val:4.1f}%]"
        improved = "교체" if bidx != 0 else "유지"
        print(f"gen {gen:>3}  챔프마진(공통맵)={F[bidx]:+.2f} {improved}  "
              f"sigma={sigma:.3f}  {time.time()-t0:.1f}s{valstr}")
        sys.stdout.flush()

    pool.shutdown(wait=False)
    print(f"# 완료 → {args.out}")


if __name__ == "__main__":
    main()
