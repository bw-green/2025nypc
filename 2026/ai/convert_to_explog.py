import sys, glob, os, re
out=[]
for d in sys.argv[1:]:
    for p in sorted(glob.glob(os.path.join(d,"game_*.txt"))):
        lines=open(p).read().split("\n")
        bi=lines.index("BOARD"); rows=lines[bi+1:bi+11]
        out.append("INIT"); out.extend(rows)
        for ln in lines:
            m=re.match(r"STEP \d+ P(\d) MOVE (-?\d+) (-?\d+) (-?\d+) (-?\d+)",ln)
            if m:
                kw="FIRST" if m.group(1)=="1" else "SECOND"
                out.append(f"{kw} {m.group(2)} {m.group(3)} {m.group(4)} {m.group(5)}")
        out.append("FINISH")
sys.stdout.write("\n".join(out)+"\n")
