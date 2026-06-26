#!/usr/bin/env python3
"""
TimeWeights(5개: bias w_prog w_threat w_cand w_margin) 를 '실제 시간제한' self-play ES 로 학습.
  - 시간관리는 시간제한 환경에서만 효과 → fitness 는 REAL-TIME(strict) 게임 승률.
  - bot_submit argv[1]=후보weight(고정), argv[2]=시간weight(후보).
  - 엘리트 (1+λ) ES. 상대=bot_strong (real-time). (강한 상대 있으면 --strong 으로 교체)
사용: python train_time_es.py --gens 30 --time 10000
출력: time_weights.txt (+.best). bot_submit.cpp DEFAULT_TIME 에 붙여넣기.
"""
import argparse, concurrent.futures as cf, os, random, sys, tempfile, time
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import referee
NP=5
DEF=np.array([-4.0,6.0,2.0,0.0,-0.4])

def ww(path,w):
    with open(path,"w") as f: f.write(" ".join(f"{x:.6g}" for x in w))

def play(t):
    idx,bot,evw,tw,strong,total,seed=t
    rng=random.Random(seed); rows=referee.random_board(rng)
    a_first=(idx%2==0); bc=[bot,evw,tw]; sc=[strong]
    fc,scmd,side=(bc,sc,1) if a_first else (sc,bc,2)
    os.environ.pop("FIXED_DEPTH",None)
    res=referee.play_game(fc,scmd,rows,total_ms=total,grace_ms=1000,strict_time=True,verbose=False)
    w=res["winner"]; reason=res.get("reason","")
    # 내 봇 몰수(타임아웃)면 강한 페널티
    to = ("몰수" in reason) and (w!=side) and (w!=0)
    r = 0 if w==0 else (1 if w==side else -1)
    return r, to

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--bot",default="./bot_submit.exe")
    ap.add_argument("--evw",default="learned_weights.txt")
    ap.add_argument("--strong",default="./bot_strong.exe")
    ap.add_argument("--out",default="time_weights.txt")
    ap.add_argument("--gens",type=int,default=30)
    ap.add_argument("--pop",type=int,default=8)
    ap.add_argument("--games",type=int,default=12)
    ap.add_argument("--time",type=int,default=10000)
    ap.add_argument("--sigma",type=float,default=0.4)
    ap.add_argument("--decay",type=float,default=0.985)
    ap.add_argument("--workers",type=int,default=8)
    ap.add_argument("--seed",type=int,default=0)
    args=ap.parse_args()
    rng=np.random.RandomState(args.seed); grng=random.Random(args.seed)
    champ=DEF.copy()
    tmp=tempfile.mkdtemp(prefix="tw_"); pool=cf.ThreadPoolExecutor(max_workers=args.workers)
    sigma=args.sigma; best=-1
    print(f"# TimeWeights ES gens={args.gens} pop={args.pop} games={args.games} time={args.time}ms")
    for gen in range(1,args.gens+1):
        t0=time.time()
        seeds=[grng.randint(0,2**31-1) for _ in range(args.games)]
        cands=[champ.copy()]+[champ+sigma*rng.randn(NP) for _ in range(args.pop)]
        twf=[]
        for k,c in enumerate(cands):
            f=os.path.join(tmp,f"t{k}.txt"); ww(f,c); twf.append(f)
        tasks=[]
        for k,f in enumerate(twf):
            for gi,sd in enumerate(seeds):
                tasks.append((gi,args.bot,args.evw,f,args.strong,args.time,sd))
        res=list(pool.map(play,tasks)); per=args.games
        F=np.array([np.mean([1 if r>0 else (0 if r==0 else -1) for r,_ in res[k*per:(k+1)*per]]) for k in range(len(cands))])
        TO=np.array([sum(1 for _,to in res[k*per:(k+1)*per] if to) for k in range(len(cands))])
        F=F-0.5*TO  # 타임아웃 페널티
        bi=int(np.argmax(F)); champ=cands[bi]; sigma*=args.decay
        ww(args.out,champ)
        # 승률(전체) 환산
        win=np.mean([1 if r>0 else 0 for r,_ in res[bi*per:(bi+1)*per]])*100
        if win>best: best=win; ww(args.out+".best",champ)
        print(f"gen{gen:>3} 적합도={F[bi]:+.2f} 승률~={win:4.1f}% 타임아웃={TO[bi]} sigma={sigma:.3f} {time.time()-t0:.0f}s  w={np.round(champ,2)}")
        sys.stdout.flush()
    print(f"# done best~={best:.1f}% -> {args.out}")

if __name__=="__main__": main()
