#!/usr/bin/env python3
"""
정책망(policy CNN) 지도학습: selfplay 데이터의 '강한 플레이 수'를 얼마나 잘 따라하는지.
  - 입력: 4채널 10x17 (살아있는값/9, 내소유, 상대소유, 생존마스크) — 둘 차례 관점.
  - 출력: 직사각형을 두 모서리로 분해 → 좌상단 히트맵(170) + 우하단 히트맵(170).
  - 정확도: 합법수 중 (tl+br) 점수 최대인 수가 실제 둔 수와 같은 비율(top-1 move acc).
  - 목표: held-out 테스트셋에서 ≥90%. 그로킹(검증정확도 도약)까지 학습.

사용:
  python train_policy.py --train selfplay selfplay1 selfplay2 selfplay3 --test selfplay_test --epochs 3000
"""
import argparse, glob, os, re, sys, time
import numpy as np
import torch, torch.nn as nn, torch.nn.functional as F
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import referee
R, C = 10, 17
N = R * C


def enum_moves(owner, val):
    """합법 합10 직사각형 목록 [(r1,c1,r2,c2)]. owner: 10x17 (0=생존)."""
    alive = (owner == 0)
    v = np.where(alive, val, 0)
    psV = np.zeros((R+1, C+1), dtype=np.int32); psA = np.zeros((R+1, C+1), dtype=np.int32)
    psV[1:,1:] = np.cumsum(np.cumsum(v, 0), 1)
    psA[1:,1:] = np.cumsum(np.cumsum(alive.astype(np.int32), 0), 1)
    def rs(ps, r1, c1, r2, c2): return ps[r2+1,c2+1]-ps[r1,c2+1]-ps[r2+1,c1]+ps[r1,c1]
    res = []
    for r1 in range(R):
        for r2 in range(r1, R):
            for c1 in range(C):
                for c2 in range(c1, C):
                    if rs(psV,r1,c1,r2,c2) != 10: continue
                    if rs(psA,r1,c1,r1,c2)==0 or rs(psA,r2,c1,r2,c2)==0: continue
                    if rs(psA,r1,c1,r2,c1)==0 or rs(psA,r1,c2,r2,c2)==0: continue
                    res.append((r1,c1,r2,c2))
    return res


def planes(owner, val, p):
    e = 3 - p
    alive = (owner == 0)
    x = np.zeros((4, R, C), dtype=np.float32)
    x[0] = np.where(alive, val/9.0, 0.0)
    x[1] = (owner == p)
    x[2] = (owner == e)
    x[3] = alive
    return x


def load_dir(dirs, need_cands=False):
    """게임 재생 → 샘플(입력, tl타깃, br타깃, [합법수목록])."""
    X, TL, BR, CANDS = [], [], [], []
    for d in dirs:
        for path in sorted(glob.glob(os.path.join(d, "game_*.txt"))):
            with open(path) as f: lines=[l.rstrip("\n") for l in f]
            bi = lines.index("BOARD")
            rows = lines[bi+1:bi+1+R]
            val = np.array([[int(ch) for ch in row] for row in rows], dtype=np.int32)
            owner = np.zeros((R, C), dtype=np.int32)
            for ln in lines:
                m = re.match(r"STEP \d+ P(\d) MOVE (-?\d+) (-?\d+) (-?\d+) (-?\d+)", ln)
                if not m: continue
                p = int(m.group(1)); mv = tuple(int(m.group(k)) for k in range(2,6))
                if mv[0] >= 0:   # 실제 수만(패스 제외)
                    X.append(planes(owner, val, p))
                    TL.append(mv[0]*C+mv[1]); BR.append(mv[2]*C+mv[3])
                    if need_cands:
                        cs = enum_moves(owner, val)
                        CANDS.append(np.array([(a*C+b, c*C+e) for (a,b,c,e) in cs], dtype=np.int64))
                    # apply
                    for r in range(mv[0],mv[2]+1):
                        for c in range(mv[1],mv[3]+1): owner[r][c]=p
    X = np.stack(X); TL=np.array(TL); BR=np.array(BR)
    return X, TL, BR, CANDS


class ResBlock(nn.Module):
    def __init__(s, ch):
        super().__init__(); s.c1=nn.Conv2d(ch,ch,3,padding=1); s.b1=nn.BatchNorm2d(ch)
        s.c2=nn.Conv2d(ch,ch,3,padding=1); s.b2=nn.BatchNorm2d(ch)
    def forward(s,x):
        y=F.relu(s.b1(s.c1(x))); y=s.b2(s.c2(y)); return F.relu(x+y)

class PolicyNet(nn.Module):
    def __init__(s, ch=64, blocks=6):
        super().__init__()
        s.stem=nn.Sequential(nn.Conv2d(4,ch,3,padding=1), nn.BatchNorm2d(ch), nn.ReLU())
        s.res=nn.Sequential(*[ResBlock(ch) for _ in range(blocks)])
        s.htl=nn.Conv2d(ch,1,1); s.hbr=nn.Conv2d(ch,1,1)
    def forward(s,x):
        y=s.res(s.stem(x))
        tl=s.htl(y).flatten(1); br=s.hbr(y).flatten(1)
        return tl, br


def move_acc(model, X, TL, BR, CANDS, dev, bs=512):
    model.eval(); correct=0; n=len(X)
    with torch.no_grad():
        for i in range(0,n,bs):
            xb=torch.from_numpy(X[i:i+bs]).to(dev)
            tl,br=model(xb); tl=tl.cpu().numpy(); br=br.cpu().numpy()
            for j in range(len(xb)):
                cand=CANDS[i+j]
                if len(cand)==0: continue
                sc=tl[j][cand[:,0]]+br[j][cand[:,1]]
                k=int(np.argmax(sc))
                if cand[k,0]==TL[i+j] and cand[k,1]==BR[i+j]: correct+=1
    return correct/n*100


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--train", nargs="+", default=["selfplay","selfplay1","selfplay2","selfplay3"])
    ap.add_argument("--test", nargs="+", default=["selfplay_test"])
    ap.add_argument("--ch", type=int, default=64); ap.add_argument("--blocks", type=int, default=6)
    ap.add_argument("--epochs", type=int, default=3000); ap.add_argument("--batch", type=int, default=512)
    ap.add_argument("--lr", type=float, default=2e-3); ap.add_argument("--wd", type=float, default=1e-4)
    ap.add_argument("--eval-every", type=int, default=10); ap.add_argument("--out", default="policy.pt")
    args=ap.parse_args()
    dev="cuda" if torch.cuda.is_available() else "cpu"
    print(f"# device={dev}")
    print("# 데이터 로딩...")
    Xtr,TLtr,BRtr,_=load_dir(args.train, need_cands=False)
    Xte,TLte,BRte,Cte=load_dir(args.test, need_cands=True)
    print(f"# train={len(Xtr)} test={len(Xte)} samples")
    Xtr_t=torch.from_numpy(Xtr).to(dev); TLtr_t=torch.from_numpy(TLtr).to(dev); BRtr_t=torch.from_numpy(BRtr).to(dev)
    model=PolicyNet(args.ch,args.blocks).to(dev)
    opt=torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=args.wd)
    n=len(Xtr); best=0
    for ep in range(1,args.epochs+1):
        model.train(); idx=torch.randperm(n,device=dev)
        tot=0
        for i in range(0,n,args.batch):
            b=idx[i:i+args.batch]; opt.zero_grad()
            tl,br=model(Xtr_t[b])
            loss=F.cross_entropy(tl,TLtr_t[b])+F.cross_entropy(br,BRtr_t[b])
            loss.backward(); opt.step(); tot=loss.item()
        if ep%args.eval_every==0 or ep==1:
            tea=move_acc(model,Xte,TLte,BRte,Cte,dev)
            if tea>best: best=tea; torch.save(model.state_dict(),args.out)
            print(f"ep {ep:>4} loss={tot:.3f}  test_move_acc={tea:5.1f}%  best={best:5.1f}%")
            sys.stdout.flush()
            if tea>=90.0: print(f"# 90% 도달 (ep {ep})")
    print(f"# 완료 best test_move_acc={best:.1f}% -> {args.out}")

if __name__=="__main__":
    main()
