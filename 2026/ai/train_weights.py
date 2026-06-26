#!/usr/bin/env python3
"""
LinearMoveEvaluator weight 학습(imitation): 각 ply에서 expert 후보가 최고점이 되도록
group-softmax cross-entropy 로 17개 weight 학습. bot_submit.cpp DEFAULT_WEIGHTS 로 출력.
"""
import sys, numpy as np, torch
try:
    import pandas as pd
    def load(p):
        d = pd.read_csv(p); return d.values.astype(np.float64)
except Exception:
    def load(p): return np.loadtxt(p, delimiter=",", skiprows=1)

FN = ["gain","neutral","stolen","own","area","diff_after","mycand_after","opcand_after",
      "mybest_after","opbest_after","mytop3gain_after","optop3gain_after",
      "mytop3steal_after","optop3steal_after","block","is_pass","is_endgame"]

def prep(arr, dev):
    gid=arr[:,0]; ply=arr[:,1]; isE=arr[:,4]; F=arr[:,5:5+17]
    key=gid*100000+ply
    grp=np.zeros(len(key),dtype=np.int64)
    grp[1:]=np.cumsum(key[1:]!=key[:-1])
    G=int(grp[-1])+1
    exp_row=np.full(G,-1,dtype=np.int64)
    er=np.where(isE>0.5)[0]
    exp_row[grp[er]]=er
    keep=exp_row>=0
    return (torch.tensor(F,dtype=torch.float32,device=dev),
            torch.tensor(grp,device=dev), G,
            torch.tensor(exp_row,device=dev), torch.tensor(keep,device=dev))

def seg_logsumexp_and_acc(logit, grp, G, exp_row, keep):
    gmax=torch.full((G,),-1e30,device=logit.device).scatter_reduce(0,grp,logit,reduce="amax",include_self=True)
    e=torch.exp(logit-gmax[grp])
    Z=torch.zeros(G,device=logit.device).index_add(0,grp,e)
    logZ=torch.log(Z)+gmax
    el=logit[exp_row.clamp(min=0)]                      # expert logit per group
    logP=el-logZ                                        # log prob of expert
    loss=-(logP[keep]).mean()
    # top-1: expert is (tied) max ; top-3: rank<3
    top1=((el>=gmax-1e-6)[keep]).float().mean().item()*100
    greater=(logit>el[grp]+1e-6).float()
    rank=torch.zeros(G,device=logit.device).index_add(0,grp,greater)
    top3=((rank<3)[keep]).float().mean().item()*100
    return loss,top1,top3

def main():
    dev="cuda" if torch.cuda.is_available() else "cpu"
    print(f"# device={dev}  로딩...")
    tr=load("train.csv"); te=load("test.csv")
    Ftr,gtr,Gtr,etr,ktr=prep(tr,dev)
    Fte,gte,Gte,ete,kte=prep(te,dev)
    print(f"# train plies={int(ktr.sum())}  test plies={int(kte.sum())}  feats=17")
    mu=Ftr.mean(0); sd=Ftr.std(0); sd=torch.where(sd<1e-6,torch.ones_like(sd),sd)
    Ztr=(Ftr-mu)/sd; Zte=(Fte-mu)/sd
    w=torch.zeros(17,device=dev,requires_grad=True)
    opt=torch.optim.Adam([w],lr=0.05,weight_decay=1e-4)
    best=0
    for ep in range(1,801):
        opt.zero_grad()
        loss,_,_=seg_logsumexp_and_acc(Ztr@w,gtr,Gtr,etr,ktr)
        loss.backward(); opt.step()
        if ep%50==0 or ep==1:
            with torch.no_grad():
                _,tr1,tr3=seg_logsumexp_and_acc(Ztr@w,gtr,Gtr,etr,ktr)
                _,te1,te3=seg_logsumexp_and_acc(Zte@w,gte,Gte,ete,kte)
            if te1>best: best=te1
            print(f"ep {ep:>3} loss={loss.item():.4f}  train top1/3={tr1:4.1f}/{tr3:4.1f}%  "
                  f"test top1/3={te1:4.1f}/{te3:4.1f}%")
            sys.stdout.flush()
    # 표준화 weight → 원시 feature weight (랭킹 불변: w_raw = w_std/sd)
    wr=(w.detach()/sd).cpu().numpy()
    print("\n# === 학습된 DEFAULT_WEIGHTS (bot_submit.cpp 에 붙여넣기) ===")
    print("static EvalWeights DEFAULT_WEIGHTS = {{")
    for i,name in enumerate(FN):
        print(f"    /*{name}*/ {wr[i]:.6g},")
    print("}};")
    with open("learned_weights.txt","w") as f:
        f.write(",".join(f"{x:.8g}" for x in wr)+"\n")
    print(f"# best test top1={best:.1f}%  (learned_weights.txt 저장)")

if __name__=="__main__":
    main()
