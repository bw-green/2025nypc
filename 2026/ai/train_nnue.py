#!/usr/bin/env python3
"""
GPU 기반 NNUE 가치망 강화학습 (self-play / vs bot_strong, 매 게임 랜덤 맵).

루프 (AlphaZero-lite, value 학습):
  1) 현재 가치망(nnue_bot)으로 self-play / vs bot_strong 게임을 16코어 병렬로 다수 생성.
     - 매 게임 새 랜덤 맵.
     - nnue_bot 은 자기 턴마다 위치 feature(510차원, 내 관점)를 dump 파일에 기록.
  2) 게임이 끝나면 각 위치를 "그 위치의 수를 둔 쪽이 최종 승리했는가"로 라벨링(+1/0/-1).
  3) 그 (feature, value) 데이터로 GPU에서 가치망을 학습(MSE, tanh 출력).
  4) 학습한 가중치를 C++ 가 읽는 텍스트 포맷으로 export → 다음 세대 nnue_bot 이 사용.
  5) 매 세대 bot_strong 과 벤치마크 대국으로 승률(진척) 측정.
반복할수록 데이터 품질↑ → 망↑ → 데이터↑ (강화학습 루프). GPU는 (3) 학습을 가속.

사용:
  g++ -std=c++17 -O2 -march=native -o nnue_bot.exe nnue_bot.cpp
  g++ -std=c++17 -O2 -o bot_strong.exe bot_strong.cpp
  python train_nnue.py --gens 100 --games 64 --depth 3 --opponent mix
"""
import argparse
import collections
import concurrent.futures as cf
import os
import random
import sys
import tempfile
import time

import numpy as np
import torch
import torch.nn as nn

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import referee  # noqa: E402

# C++ nnue_bot.cpp 와 반드시 일치해야 하는 상수
R, C = 10, 17
BOARD = 3 * R * C   # 510
EXTRA = 10
IN = BOARD + EXTRA   # 520
H1, H2 = 20, 4       # ~1만 파라미터 (nnue_bot.cpp 와 동일)


# ---------------- 신경망 (C++ forward 와 동일 구조) ----------------
class ValueNet(nn.Module):
    def __init__(self, dropout=0.1):
        super().__init__()
        self.fc1 = nn.Linear(IN, H1)
        self.fc2 = nn.Linear(H1, H2)
        self.fc3 = nn.Linear(H2, 1)
        self.drop = nn.Dropout(dropout)   # 학습 때만 적용(추론·C++은 off) → 과적합 억제

    def forward(self, x):
        x = self.drop(torch.relu(self.fc1(x)))
        x = self.drop(torch.relu(self.fc2(x)))
        return self.fc3(x)   # 선형 출력(포화 없음) — C++ Net::forward 와 동일


def export_weights(model, path):
    """C++ nnue_bot.cpp 가 읽는 텍스트 포맷으로 가중치 저장."""
    model_cpu = model.to("cpu")
    parts = ["NNUE 1", f"{IN} {H1} {H2} 1"]

    def flat(t):
        return " ".join(f"{v:.6f}" for v in t.detach().numpy().reshape(-1))

    parts.append(flat(model_cpu.fc1.weight))   # [H1, IN] row-major
    parts.append(flat(model_cpu.fc1.bias))     # [H1]
    parts.append(flat(model_cpu.fc2.weight))   # [H2, H1]
    parts.append(flat(model_cpu.fc2.bias))     # [H2]
    parts.append(flat(model_cpu.fc3.weight))   # [1, H2]
    parts.append(flat(model_cpu.fc3.bias))     # [1]
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        f.write("\n".join(parts) + "\n")
    os.replace(tmp, path)   # 원자적 교체 (병렬 게임이 읽는 중 깨지지 않게)


# ---------------- 한 게임 실행 + 라벨링 ----------------
def _read_dump(path):
    if not os.path.exists(path):
        return np.empty((0, IN), dtype=np.float32)
    rows = []
    with open(path) as f:
        for ln in f:
            ln = ln.strip()
            if not ln:
                continue
            vals = ln.split()
            if len(vals) == IN:
                rows.append(np.array(vals, dtype=np.float32))
    if not rows:
        return np.empty((0, IN), dtype=np.float32)
    return np.stack(rows)


def play_one(args_tuple):
    """
    한 게임 실행. nnue 쪽 위치들을 (feature, label)로 반환.
    반환: (X[np], y[np], strong_result)  strong_result: 1 nnue승,0 무,-1 패, None(strong 미참여)
    """
    (idx, mode, bot_exe, strong_exe, weights, depth, seed, tmpdir) = args_tuple
    rng = random.Random(seed)
    rows = referee.random_board(rng)

    dumpA = os.path.join(tmpdir, f"g{idx}_A.txt")
    dumpB = os.path.join(tmpdir, f"g{idx}_B.txt")
    for p in (dumpA, dumpB):
        try:
            os.remove(p)
        except OSError:
            pass

    nnue_first = [bot_exe, weights, dumpA]
    nnue_second = [bot_exe, weights, dumpB]
    strong_cmd = [strong_exe]

    a_first = (idx % 2 == 0)   # swap 으로 선후공 공정화

    if mode == "self":
        first_cmd, second_cmd = nnue_first, nnue_second
        first_is_nnue, second_is_nnue = True, True
    else:  # 'strong': nnue vs bot_strong
        if a_first:
            first_cmd, second_cmd = nnue_first, strong_cmd
            first_is_nnue, second_is_nnue = True, False
        else:
            first_cmd, second_cmd = strong_cmd, nnue_second
            first_is_nnue, second_is_nnue = False, True

    os.environ["FIXED_DEPTH"] = str(depth)
    res = referee.play_game(first_cmd, second_cmd, rows,
                            total_ms=1_000_000, grace_ms=5000,
                            strict_time=False, verbose=False)
    winner = res["winner"]   # 1 first, 2 second, 0 draw
    fs, ss = res["first_score"], res["second_score"]

    Xs, ys = [], []

    def label_for(side):  # side: 1 or 2
        # 승/패(±1)가 아니라 최종 점수차 기반 연속 라벨 → "얼마나" 이겼는지 학습.
        margin = (fs - ss) if side == 1 else (ss - fs)
        return float(np.tanh(margin / 20.0))

    if first_is_nnue:
        x = _read_dump(dumpA)
        if len(x):
            Xs.append(x)
            ys.append(np.full(len(x), label_for(1), dtype=np.float32))
    if second_is_nnue:
        x = _read_dump(dumpB)
        if len(x):
            Xs.append(x)
            ys.append(np.full(len(x), label_for(2), dtype=np.float32))

    for p in (dumpA, dumpB):
        try:
            os.remove(p)
        except OSError:
            pass

    # strong 벤치마크 결과 (strong 모드일 때만)
    strong_result = None
    if mode == "strong":
        nnue_side = 1 if first_is_nnue else 2
        if winner == 0:
            strong_result = 0
        else:
            strong_result = 1 if winner == nnue_side else -1

    if Xs:
        return np.concatenate(Xs), np.concatenate(ys), strong_result
    return (np.empty((0, IN), dtype=np.float32),
            np.empty((0,), dtype=np.float32), strong_result)


def gen_games(pool, n, mode, bot_exe, strong_exe, weights, depth, rng, tmpdir):
    tasks = [(i, mode, bot_exe, strong_exe, weights, depth,
              rng.randint(0, 2**31 - 1), tmpdir) for i in range(n)]
    Xs, ys = [], []
    s_win = s_draw = s_loss = 0
    for X, y, sr in pool.map(play_one, tasks):
        if len(X):
            Xs.append(X)
            ys.append(y)
        if sr == 1:
            s_win += 1
        elif sr == 0 and mode == "strong":
            s_draw += 1
        elif sr == -1:
            s_loss += 1
    if Xs:
        X = np.concatenate(Xs)
        y = np.concatenate(ys)
    else:
        X = np.empty((0, IN), dtype=np.float32)
        y = np.empty((0,), dtype=np.float32)
    return X, y, (s_win, s_draw, s_loss)


def eval_vs_strong(pool, n, bot_exe, strong_exe, weights, depth, rng, tmpdir,
                   fixed=True):
    """벤치마크 전용: nnue vs bot_strong, swap. (승, 무, 패) 반환.
    fixed=True: 고정 맵 세트(세대 간 비교용). fixed=False: 매번 새 랜덤 맵(일반성 측정용)."""
    def board_seed(i):
        return (777_000 + i) if fixed else rng.randint(0, 2**31 - 1)
    tasks = [(10_000 + i, "strong", bot_exe, strong_exe, weights, depth,
              board_seed(i), tmpdir) for i in range(n)]
    w = d = l = 0
    for _, _, sr in pool.map(play_one, tasks):
        if sr == 1:
            w += 1
        elif sr == 0:
            d += 1
        elif sr == -1:
            l += 1
    return w, d, l


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bot", default="./nnue_bot.exe", help="nnue_bot 실행파일")
    ap.add_argument("--strong", default="./bot_strong.exe", help="상대(bot_strong) 실행파일")
    ap.add_argument("--out", default="nnue_weights.txt", help="가중치 출력(=nnue_bot 입력)")
    ap.add_argument("--init", default=None, help="이어서 학습할 시작 가중치 파일")
    ap.add_argument("--gens", type=int, default=100, help="세대 수")
    ap.add_argument("--games", type=int, default=64, help="세대당 데이터 생성 게임 수")
    ap.add_argument("--depth", type=int, default=3, help="학습/벤치 고정 탐색 깊이")
    ap.add_argument("--opponent", choices=["self", "strong", "mix"], default="mix",
                    help="데이터 생성 상대: self-play / vs strong / 둘 혼합")
    ap.add_argument("--eval-games", type=int, default=24, help="벤치 시 strong 게임 수")
    ap.add_argument("--eval-every", type=int, default=5, help="N세대마다 strong 벤치(CPU 절약)")
    ap.add_argument("--epochs", type=int, default=2, help="세대당 학습 epoch(과적합 억제 위해 작게)")
    ap.add_argument("--batch", type=int, default=4096)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--wd", type=float, default=1e-4, help="weight decay(과적합 억제)")
    ap.add_argument("--dropout", type=float, default=0.1)
    ap.add_argument("--train-sample", type=int, default=120_000,
                    help="세대마다 버퍼에서 학습에 쓸 표본 수(전체 반복=과적합 방지)")
    ap.add_argument("--buffer", type=int, default=400_000, help="리플레이 버퍼 최대 샘플 수")
    ap.add_argument("--workers", type=int, default=8, help="동시 게임 수")
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()

    dev = "cuda" if torch.cuda.is_available() else "cpu"
    print(f"# device={dev}  {torch.cuda.get_device_name(0) if dev=='cuda' else ''}")
    print(f"# gens={args.gens} games/gen={args.games} depth={args.depth} "
          f"opponent={args.opponent} workers={args.workers}")

    rng = random.Random(args.seed)
    torch.manual_seed(args.seed)

    model = ValueNet(dropout=args.dropout).to(dev)
    if args.init and os.path.exists(args.init):
        print(f"# (init 파일은 텍스트 포맷이므로 무시하고 새 망에서 시작합니다: {args.init})")
    opt = torch.optim.Adam(model.parameters(), lr=args.lr, weight_decay=args.wd)
    lossf = nn.MSELoss()

    # gen 0: 랜덤 초기 망 export → 첫 데이터 생성에 사용
    export_weights(model, args.out)
    model.to(dev)

    buf_X = collections.deque()
    buf_y = collections.deque()
    buf_n = 0

    tmpdir = tempfile.mkdtemp(prefix="nnue_train_")
    pool = cf.ThreadPoolExecutor(max_workers=args.workers)

    best_rate = -1.0
    try:
        for gen in range(1, args.gens + 1):
            t0 = time.time()

            # 1) 데이터 생성 (매 게임 랜덤 맵)
            if args.opponent == "mix":
                mode = "self" if gen % 2 == 1 else "strong"
            else:
                mode = args.opponent
            X, y, (sw, sd, sl) = gen_games(
                pool, args.games, mode, args.bot, args.strong,
                args.out, args.depth, rng, tmpdir)

            # 2) 리플레이 버퍼에 적재 (오래된 것부터 제거)
            if len(X):
                buf_X.append(X)
                buf_y.append(y)
                buf_n += len(X)
            while buf_n > args.buffer and len(buf_X) > 1:
                buf_n -= len(buf_X.popleft())
                buf_y.popleft()

            if buf_n == 0:
                print(f"gen {gen:>3}: 데이터 없음(스킵)")
                continue

            fullX = np.concatenate(buf_X)
            fully = np.concatenate(buf_y)
            # 버퍼 전체를 매 세대 반복하면 외워버림 → 매번 무작위 표본만 학습.
            tot = len(fullX)
            if tot > args.train_sample:
                sel = np.random.choice(tot, args.train_sample, replace=False)
                fullX, fully = fullX[sel], fully[sel]
            allX = torch.from_numpy(fullX).to(dev)
            ally = torch.from_numpy(fully).to(dev).unsqueeze(1)

            # 3) GPU 학습
            model.train()
            n = allX.shape[0]
            last_loss = 0.0
            for _ in range(args.epochs):
                perm = torch.randperm(n, device=dev)
                for i in range(0, n, args.batch):
                    bi = perm[i:i + args.batch]
                    opt.zero_grad()
                    out = model(allX[bi])
                    loss = lossf(out, ally[bi])
                    loss.backward()
                    opt.step()
                    last_loss = loss.item()

            # 4) export
            export_weights(model, args.out)
            model.to(dev)

            # 5) 벤치마크 vs strong (eval_every 세대마다만 → CPU 절약)
            do_eval = (gen % args.eval_every == 0) or (gen == 1)
            if do_eval:
                ew, ed, el = eval_vs_strong(pool, args.eval_games, args.bot,
                                            args.strong, args.out, args.depth,
                                            rng, tmpdir)
                decided = ew + el
                rate = ew / decided if decided else 0.0
                if rate > best_rate:
                    best_rate = rate
                    export_weights(model, args.out + ".best")
                    model.to(dev)
                bench = (f"vs_strong 승/무/패={ew}/{ed}/{el} 승률={rate*100:4.1f}% "
                         f"(best {best_rate*100:4.1f}%)")
            else:
                bench = "(벤치 생략)"

            dt = time.time() - t0
            mtag = ""
            if mode == "strong":
                mtag = f" [gen데이터 strong: {sw}/{sd}/{sl}]"
            print(f"gen {gen:>3}  mode={mode:<6} samples={n:>7} loss={last_loss:.4f}  "
                  f"{bench}  {dt:4.1f}s{mtag}")
            sys.stdout.flush()
    finally:
        pool.shutdown(wait=False)
        print(f"\n# 완료. 최종 가중치 -> {args.out} (best -> {args.out}.best)")
        print(f"# 사용: ./{os.path.basename(args.bot)} {args.out}")


if __name__ == "__main__":
    main()
