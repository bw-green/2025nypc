import argparse, concurrent.futures as cf, os, random, sys
sys.path.insert(0,'.'); import referee
def one(t):
    b, sub, strong, total, seed, sub_first = t
    rows=referee.random_board(random.Random(seed))
    fc=[*sub.split()] if sub_first else [strong]
    sc=[strong] if sub_first else [*sub.split()]
    os.environ.pop("FIXED_DEPTH",None)
    r=referee.play_game(fc,sc,rows,total_ms=total,grace_ms=1000,strict_time=True,verbose=False)
    w=r["winner"]; side=1 if sub_first else 2
    res = 0 if w==0 else (1 if w==side else -1)
    to = ("몰수" in r.get("reason","")) and res==-1
    return (sub_first,res,to)
ap=argparse.ArgumentParser()
ap.add_argument("--sub",default="./bot_submit.exe"); ap.add_argument("--strong",default="./bot_strong.exe")
ap.add_argument("--boards",type=int,default=120); ap.add_argument("--time",type=int,default=10000)
ap.add_argument("--workers",type=int,default=8); ap.add_argument("--seed",type=int,default=1234)
a=ap.parse_args()
tasks=[]
for b in range(a.boards):
    sd=a.seed+b
    tasks.append((b,a.sub,a.strong,a.time,sd,True))
    tasks.append((b,a.sub,a.strong,a.time,sd,False))
fw=fl=fd=sw=sl=sd_=to=0
with cf.ThreadPoolExecutor(max_workers=a.workers) as p:
    for sub_first,res,t in p.map(one,tasks):
        if t: to+=1
        if sub_first:
            if res>0: fw+=1
            elif res<0: fl+=1
            else: fd+=1
        else:
            if res>0: sw+=1
            elif res<0: sl+=1
            else: sd_+=1
fdec=fw+fl; sdec=sw+sl; tot=fw+sw+fl+sl
print(f"boards={a.boards} (게임 {a.boards*2}), 실시간 {a.time}ms, 타임아웃 {to}")
print(f"  선공(bot_submit): {fw}승 {fl}패 {fd}무  -> {fw/fdec*100:.1f}%")
print(f"  후공(bot_submit): {sw}승 {sl}패 {sd_}무  -> {sw/sdec*100:.1f}%")
print(f"  종합(seat-paired): {fw+sw}승 {fl+sl}패  -> {(fw+sw)/tot*100:.1f}%  (전체기준 {(fw+sw)/(a.boards*2)*100:.1f}%)")
