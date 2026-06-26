#!/usr/bin/env python3
"""
패착 기반 시간배분 학습: 업데이트봇(tw_upd) vs 고정봇(tw_fixed) 실시간 대결.
  - 둘 다 같은 평가/탐색(bot_submit), 시간배분 weight만 다름 → 시간배분만 격리 학습.
  - 업데이트봇이 지면: 패착(점수 가장 크게 떨어진 내 수)·시점을 blunder_log.txt 에 기록,
      그 상황의 시간feature φ로 'frac↑'(시간 부족했다고 보고 증가),
      반대로 '후보 많고 위협 낮은(고민 불필요)' 상황은 'frac↓'.
  - 핵심 지표: 업데이트봇의 vs고정봇 승률(50% 넘어 오르면 시간배분이 개선된 것).
출력: tw_upd.txt (학습된 5개 시간weight). bot_submit.cpp DEFAULT_TIME 에 반영 가능.
"""
import argparse, os, random, sys, time
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import referee
R, C = 10, 17
TWN = 5  # bias, w_prog, w_threat, w_cand, w_margin

def ww(path, w):
    with open(path, "w") as f: f.write(" ".join(f"{x:.6g}" for x in w))

def enum(owner, val):
    al=(owner==0); v=np.where(al,val,0)
    psV=np.zeros((R+1,C+1),int); psA=np.zeros((R+1,C+1),int)
    psV[1:,1:]=np.cumsum(np.cumsum(v,0),1); psA[1:,1:]=np.cumsum(np.cumsum(al.astype(int),0),1)
    def q(p,r1,c1,r2,c2): return p[r2+1,c2+1]-p[r1,c2+1]-p[r2+1,c1]+p[r1,c1]
    res=[]
    for r1 in range(R):
        for r2 in range(r1,R):
            for c1 in range(C):
                for c2 in range(c1,C):
                    if q(psV,r1,c1,r2,c2)!=10: continue
                    if q(psA,r1,c1,r1,c2)==0 or q(psA,r2,c1,r2,c2)==0: continue
                    if q(psA,r1,c1,r2,c1)==0 or q(psA,r1,c2,r2,c2)==0: continue
                    res.append((r1,c1,r2,c2))
    return res

def gains(owner, val, player):
    """후보별 gain 리스트(내림차순) + 개수."""
    e=3-player; al=(owner==0)
    cand=enum(owner,val); gs=[]
    for (r1,c1,r2,c2) in cand:
        neu=stl=0
        for r in range(r1,r2+1):
            for c in range(c1,c2+1):
                o=owner[r,c]
                if o==0: neu+=1
                elif o==e: stl+=1
        gs.append(neu+2*stl)
    gs.sort(reverse=True)
    return gs

def features(owner, val, player, myturns):
    gs=gains(owner,val,player)
    e=3-player
    # 상대 위협 = 상대 best gain
    og=gains(owner,val,e)
    diff=int((owner==player).sum())-int((owner==e).sum())
    prog=min(1.0,myturns/20.0)
    threat=min(1.0,(og[0] if og else 0)/12.0)
    cand=min(1.0,len(gs)/40.0)
    margin=min(1.0,abs(diff)/20.0)
    return np.array([1.0,prog,threat,cand,margin]), len(gs), (gs[0]-gs[1] if len(gs)>1 else (gs[0] if gs else 0))

def play(bot, evw, tw_a, tw_b, rows, total, seed, a_first):
    fc=[bot,evw,tw_a] if a_first else [bot,evw,tw_b]
    sc=[bot,evw,tw_b] if a_first else [bot,evw,tw_a]
    os.environ.pop("FIXED_DEPTH",None)
    res=referee.play_game(fc,sc,rows,total_ms=total,grace_ms=1000,strict_time=True,verbose=False)
    return res

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--bot",default="./bot_submit.exe"); ap.add_argument("--evw",default="learned_weights.txt")
    ap.add_argument("--iters",type=int,default=60); ap.add_argument("--games",type=int,default=6)
    ap.add_argument("--time",type=int,default=10000); ap.add_argument("--lr",type=float,default=0.15)
    ap.add_argument("--seed",type=int,default=0)
    args=ap.parse_args()
    rng=random.Random(args.seed)
    tw_fixed=np.array([-1.23,0,0,0,0.0]); tw_upd=tw_fixed.copy()
    import tempfile; tmp=tempfile.mkdtemp(prefix="twl_")
    ff=os.path.join(tmp,"fix.txt"); ww(ff,tw_fixed)
    fu=os.path.join(tmp,"upd.txt"); ww(fu,tw_upd)
    blog=open("blunder_log.txt","w")
    # 업데이트봇 기준 선/후공 분리 집계
    fw=fl=sw=sl=draws=0

    def learn_from_loss(rows, res, upd_side, it, g):
        val=np.array([[int(ch) for ch in row] for row in rows])
        owner=np.zeros((R,C),int); myturns=0; lead=[]; states=[]
        for (pl,mv) in res["moves"]:
            if pl==upd_side:
                fv,nc,gap=features(owner,val,upd_side,myturns)
                states.append((len(lead),fv,nc,gap)); myturns+=1
            if mv!=referee.PASS:
                r1,c1,r2,c2=mv
                for r in range(r1,r2+1):
                    for c in range(c1,c2+1): owner[r,c]=pl
            a=int((owner==upd_side).sum()); b=int((owner==(3-upd_side)).sum()); lead.append(a-b)
        if not states: return
        worst=None
        for (li,fv,nc,gap) in states:
            before=lead[li-1] if li>0 else 0
            after=lead[min(li+1,len(lead)-1)]
            drop=before-after
            if worst is None or drop>worst[0]: worst=(drop,li,fv,nc,gap)
        drop,li,fv,nc,gap=worst
        blog.write(f"iter{it} g{g} side{upd_side}: 패착 ply~{li} drop={drop} cand={nc} gap={gap} "
                   f"feat(prog,threat,cand,margin)={np.round(fv[1:],2).tolist()}\n"); blog.flush()
        nonlocal tw_upd
        tw_upd = tw_upd + args.lr*fv                       # 패착 상황=시간부족 가정 → frac↑
        easy=min(states,key=lambda s:(s[1][2], -s[2]))     # 위협↓·후보↑(고민 불필요) → frac↓
        tw_upd = tw_upd - 0.5*args.lr*easy[1]

    for it in range(1,args.iters+1):
        ww(fu,tw_upd)
        for g in range(args.games):                        # games = 보드 수
            rows=referee.random_board(random.Random(rng.randint(0,2**31-1)))
            for a_first in (True, False):                  # 같은 보드를 양쪽 다(선공 이점 상쇄)
                res=play(args.bot,args.evw,fu,ff,rows,args.time,0,a_first)
                w=res["winner"]; upd_side=1 if a_first else 2
                if w==0:
                    draws+=1
                elif a_first:
                    if w==upd_side: fw+=1
                    else: fl+=1
                else:
                    if w==upd_side: sw+=1
                    else: sl+=1
                if w!=0 and w!=upd_side:
                    learn_from_loss(rows,res,upd_side,it,g)
        if it%5==0 or it==1:
            fdec=fw+fl; sdec=sw+sl; tot=fw+sw+fl+sl
            fr=fw/fdec*100 if fdec else 0; sr=sw/sdec*100 if sdec else 0
            tr=(fw+sw)/tot*100 if tot else 0
            print(f"iter{it:>3} 업데이트봇 vs 고정봇  종합={tr:4.1f}%  "
                  f"선공{fr:4.1f}%(승{fw}패{fl})  후공{sr:4.1f}%(승{sw}패{sl})  무{draws}  "
                  f"tw_upd={np.round(tw_upd,2).tolist()}")
            sys.stdout.flush()
            fw=fl=sw=sl=draws=0
    ww("tw_upd.txt",tw_upd); blog.close()
    print(f"# done -> tw_upd.txt = {np.round(tw_upd,3).tolist()}  (blunder_log.txt)")

if __name__=="__main__": main()
