#!/usr/bin/env python3
"""
Distillation 학습: 신경망이 bot_strong 의 정적 평가를 모방하도록 지도학습.

흐름:
  1) distill_gen.exe 를 16코어 병렬로 돌려 (feature 522, bot_strong평가 타깃) 데이터를 대량 생성.
  2) GPU에서 ValueNet(=nnue_bot 과 동일 구조)을 MSE로 학습(train/val 분리, 조기종료).
  3) nnue_weights.txt 로 export → nnue_bot 이 이 평가를 사용.
  4) 고정 40판 맵에서 vs bot_strong 벤치마크.
탐색은 이미 bot_strong 과 동일(이식 완료)하므로, 평가만 잘 베끼면 ~50% 수렴 기대.

사용:
  g++ -std=c++17 -Ofast -march=native -o distill_gen.exe distill_gen.cpp
  python train_distill.py --games 6000 --epochs 60 --eval-games 40
"""
import argparse
import concurrent.futures as cf
import os
import subprocess
import sys
import tempfile
import time

import numpy as np
import torch
import torch.nn as nn

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import train_nnue as T   # ValueNet, export_weights, eval_vs_strong, IN 등 재사용
import referee  # noqa


def gen_chunk(args):
    exe, out, games, seed, eps, ldepth, lbeam = args
    subprocess.run([exe, out, str(games), str(seed), str(eps),
                    str(ldepth), str(lbeam)], check=True)
    return out


def load_data(files):
    Xs, ys = [], []
    for fp in files:
        with open(fp) as fh:
            rows = [ln.split() for ln in fh if ln.strip()]
        if not rows:
            continue
        a = np.array(rows, dtype=np.float32)
        Xs.append(a[:, :T.IN])
        ys.append(a[:, T.IN])
    X = np.concatenate(Xs)
    y = np.concatenate(ys)
    return X, y


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gen", default="./distill_gen.exe")
    ap.add_argument("--bot", default="./nnue_bot.exe")
    ap.add_argument("--strong", default="./bot_strong.exe")
    ap.add_argument("--out", default="nnue_weights.txt")
    ap.add_argument("--games", type=int, default=6000, help="총 데이터 생성 게임 수")
    ap.add_argument("--epsilon", type=float, default=0.3, help="데이터 생성 시 무작위 수 비율")
    ap.add_argument("--label-depth", type=int, default=0,
                    help="라벨을 depth-D 미니맥스 값으로(0=정적 평가). >0이면 bot_strong보다 강해질 여지")
    ap.add_argument("--label-beam", type=int, default=8, help="라벨 탐색 beam")
    ap.add_argument("--workers", type=int, default=16)
    ap.add_argument("--epochs", type=int, default=60)
    ap.add_argument("--batch", type=int, default=8192)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--wd", type=float, default=1e-5)
    ap.add_argument("--dropout", type=float, default=0.05)
    ap.add_argument("--val-frac", type=float, default=0.05)
    ap.add_argument("--eval-games", type=int, default=40, help="vs strong 벤치(고정맵)")
    ap.add_argument("--depth", type=int, default=3, help="벤치 탐색 깊이")
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()

    dev = "cuda" if torch.cuda.is_available() else "cpu"
    print(f"# device={dev}  {torch.cuda.get_device_name(0) if dev=='cuda' else ''}")
    torch.manual_seed(args.seed)
    np.random.seed(args.seed)

    tmpdir = tempfile.mkdtemp(prefix="distill_")

    # 1) 데이터 생성 (병렬)
    t0 = time.time()
    per = max(1, args.games // args.workers)
    tasks = [(args.gen, os.path.join(tmpdir, f"d{i}.txt"), per,
              args.seed * 100000 + i + 1, args.epsilon,
              args.label_depth, args.label_beam)
             for i in range(args.workers)]
    with cf.ThreadPoolExecutor(max_workers=args.workers) as pool:
        files = list(pool.map(gen_chunk, tasks))
    X, y = load_data(files)
    print(f"# 데이터 {len(X):,} 위치 생성  ({time.time()-t0:.1f}s)")

    # 2) train/val 분리
    n = len(X)
    perm = np.random.permutation(n)
    X, y = X[perm], y[perm]
    nval = int(n * args.val_frac)
    Xv = torch.from_numpy(X[:nval]).to(dev)
    yv = torch.from_numpy(y[:nval]).to(dev).unsqueeze(1)
    Xt = torch.from_numpy(X[nval:]).to(dev)
    yt = torch.from_numpy(y[nval:]).to(dev).unsqueeze(1)

    model = T.ValueNet(dropout=args.dropout).to(dev)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr, weight_decay=args.wd)
    sched = torch.optim.lr_scheduler.ReduceLROnPlateau(opt, factor=0.5, patience=4)
    lossf = nn.MSELoss()

    # 3) 학습 (조기종료: val 기준 best 저장)
    best_val = 1e9
    bad = 0
    nt = Xt.shape[0]
    for ep in range(1, args.epochs + 1):
        model.train()
        idx = torch.randperm(nt, device=dev)
        for i in range(0, nt, args.batch):
            bi = idx[i:i + args.batch]
            opt.zero_grad()
            loss = lossf(model(Xt[bi]), yt[bi])
            loss.backward()
            opt.step()
        model.eval()
        with torch.no_grad():
            vl = lossf(model(Xv), yv).item()
        sched.step(vl)
        if vl < best_val - 1e-6:
            best_val = vl
            bad = 0
            T.export_weights(model, args.out)   # val 최저 시점 저장
            model.to(dev)
            tag = " *best"
        else:
            bad += 1
            tag = ""
        if ep == 1 or ep % 5 == 0 or tag:
            print(f"ep {ep:>3}  val_mse={vl:.5f}  best={best_val:.5f}{tag}")
        sys.stdout.flush()
        if bad >= 12:
            print(f"# 조기종료 (val {best_val:.5f})")
            break

    # 4) 벤치마크 vs strong (매번 새 랜덤 맵 → 일반성 측정)
    print(f"# 벤치마크 vs bot_strong (랜덤 {args.eval_games}판, 학습 미사용 맵)...")
    import random
    os.environ["FIXED_DEPTH"] = str(args.depth)
    with cf.ThreadPoolExecutor(max_workers=args.workers) as pool:
        w, d, l = T.eval_vs_strong(pool, args.eval_games, args.bot, args.strong,
                                   args.out, args.depth,
                                   random.Random(args.seed + 99999), tmpdir,
                                   fixed=False)
    dec = w + l
    rate = w / dec * 100 if dec else 0.0
    print(f"# vs_strong 승/무/패 = {w}/{d}/{l}   승률 = {rate:.1f}%")
    print(f"# 가중치 -> {args.out}  (val_mse={best_val:.5f})")


if __name__ == "__main__":
    main()
