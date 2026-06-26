#!/usr/bin/env python3
"""
bot_submit 의 17개 LinearMoveEvaluator weight 를 self-play ES 로 추가 튜닝(모방 → 능가).
  - 엘리트 (1+λ) ES + 공통난수(CRN): 챔피언 + 섭동 후보를 같은 맵에서 bot_strong 과 대국, 마진 최대 후보 선택.
  - 시작점: 모방 학습 weight(learned_weights.txt). 검증: 고정맵 vs bot_strong 승률.
사용:
  g++ -std=c++17 -O2 -o bot_submit.exe bot_submit.cpp
  python train_es_linear.py --gens 400 --depth 3
출력: weights_es.txt (+ .best). 최종은 bot_submit.cpp DEFAULT_WEIGHTS 에 붙여넣기.
"""
import argparse, concurrent.futures as cf, os, random, sys, time
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import referee
NP = 17

def write_w(path, w):
    with open(path, "w") as f: f.write(" ".join(f"{x:.8g}" for x in w))

def play_margin(t):
    idx, bot, wf, strong, depth, seed = t
    rng = random.Random(seed); rows = referee.random_board(rng)
    a_first = (idx % 2 == 0)
    bc = [bot, wf]; sc = [strong]
    fc, scmd, side = (bc, sc, 1) if a_first else (sc, bc, 2)
    os.environ["FIXED_DEPTH"] = str(depth)
    res = referee.play_game(fc, scmd, rows, total_ms=1_000_000, grace_ms=5000,
                            strict_time=False, verbose=False)
    fs, ss = res["first_score"], res["second_score"]
    return (fs - ss) if side == 1 else (ss - fs)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bot", default="./bot_submit.exe")
    ap.add_argument("--strong", default="./bot_strong.exe")
    ap.add_argument("--init", default="learned_weights.txt")
    ap.add_argument("--out", default="weights_es.txt")
    ap.add_argument("--gens", type=int, default=400)
    ap.add_argument("--pop", type=int, default=16)
    ap.add_argument("--games", type=int, default=24)
    ap.add_argument("--depth", type=int, default=3)
    ap.add_argument("--sigma", type=float, default=0.06)
    ap.add_argument("--decay", type=float, default=0.999)
    ap.add_argument("--val-every", type=int, default=10)
    ap.add_argument("--val-games", type=int, default=60)
    ap.add_argument("--workers", type=int, default=16)
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()

    rng = np.random.RandomState(args.seed); grng = random.Random(args.seed)
    if os.path.exists(args.init):
        champ = np.array([float(x) for x in open(args.init).read().replace(",", " ").split()][:NP])
        print(f"# init from {args.init}")
    else:
        champ = np.zeros(NP); print("# init zeros")
    import tempfile
    tmp = tempfile.mkdtemp(prefix="esl_")
    pool = cf.ThreadPoolExecutor(max_workers=args.workers)
    sigma = args.sigma; best_val = -1.0

    def validate(w):
        wf = os.path.join(tmp, "val.txt"); write_w(wf, w)
        tasks = [(i, args.bot, wf, args.strong, args.depth, 777000+i) for i in range(args.val_games)]
        r = list(pool.map(play_margin, tasks))
        win = sum(1 for m in r if m > 0); los = sum(1 for m in r if m < 0)
        return (win/(win+los)*100 if win+los else 50.0), float(np.mean(r))

    print(f"# ES-linear gens={args.gens} pop={args.pop} games={args.games} depth={args.depth}")
    for gen in range(1, args.gens+1):
        t0 = time.time()
        seeds = [grng.randint(0, 2**31-1) for _ in range(args.games)]
        cands = [champ.copy()]
        for _ in range(args.pop):
            cands.append(champ + sigma*(np.abs(champ)+0.05)*rng.randn(NP))
        wfiles = []
        for k, c in enumerate(cands):
            wf = os.path.join(tmp, f"c{k}.txt"); write_w(wf, c); wfiles.append(wf)
        tasks = []
        for k, wf in enumerate(wfiles):
            for gi, sd in enumerate(seeds):
                tasks.append((gi, args.bot, wf, args.strong, args.depth, sd))
        res = list(pool.map(play_margin, tasks))
        per = args.games
        F = np.array([np.mean(res[k*per:(k+1)*per]) for k in range(len(cands))])
        champ = cands[int(np.argmax(F))]; sigma *= args.decay
        write_w(args.out, champ)
        vs = ""
        if gen % args.val_every == 0 or gen == 1:
            vr, vm = validate(champ)
            if vr > best_val: best_val = vr; write_w(args.out+".best", champ)
            vs = f"  [vs_strong 승률={vr:4.1f}% 마진={vm:+.2f} best={best_val:4.1f}%]"
        print(f"gen {gen:>3} champ마진(공통맵)={F.max():+.2f} sigma={sigma:.4f} {time.time()-t0:.1f}s{vs}")
        sys.stdout.flush()
    print(f"# 완료 best={best_val:.1f}% -> {args.out} (.best)")

if __name__ == "__main__":
    main()
