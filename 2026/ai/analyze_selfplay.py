#!/usr/bin/env python3
"""
selfplay 데이터 분석: 각 게임을 재생하며 '승자가 무엇으로 이겼는지' 추출.
  - 각 game_XXX.txt 끝에 ANALYSIS 줄 추가(업데이트)
  - 종합 리포트 analysis_summary.txt 작성
지표(플레이어별): 둔 수, 중립 점령(빈칸), 탈취(상대칸·2배), 자기칸 재점령, 선공 여부, 중반 리드/역전.
"""
import glob, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import referee
R, C = 10, 17
DIRS = ["selfplay", "selfplay1", "selfplay2", "selfplay3"]


def parse_game(path):
    with open(path) as f:
        lines = [ln.rstrip("\n") for ln in f]
    # BOARD
    i = lines.index("BOARD")
    rows = lines[i+1:i+1+R]
    moves = []
    for ln in lines:
        m = re.match(r"STEP \d+ P(\d) MOVE (-?\d+) (-?\d+) (-?\d+) (-?\d+)", ln)
        if m:
            p = int(m.group(1)); mv = tuple(int(m.group(k)) for k in range(2,6))
            moves.append((p, mv))
    res = [ln for ln in lines if ln.startswith("RESULT")]
    return rows, moves, res[0] if res else ""


def analyze(rows, moves):
    st = referee.State(rows)
    stat = {1: dict(mv=0, neu=0, steal=0, own=0), 2: dict(mv=0, neu=0, steal=0, own=0)}
    lead_first = []   # 매 수 후 (first-second)
    for p, mv in moves:
        if mv != referee.PASS:
            e = 3 - p
            r1, c1, r2, c2 = mv
            neu = steal = own = 0
            for r in range(r1, r2+1):
                for c in range(c1, c2+1):
                    o = st.owner[r][c]
                    if o == 0: neu += 1
                    elif o == e: steal += 1
                    else: own += 1
            stat[p]["mv"] += 1; stat[p]["neu"] += neu
            stat[p]["steal"] += steal; stat[p]["own"] += own
            st.apply(mv, p)
        a, b = st.score()
        lead_first.append(a - b)
    a, b = st.score()
    winner = 1 if a > b else 2 if b > a else 0
    return stat, winner, a, b, lead_first


def reason(stat, winner, a, b, lead):
    if winner == 0:
        return "무승부"
    w, l = winner, 3 - winner
    ws, ls = stat[w]["steal"], stat[l]["steal"]
    wn, ln_ = stat[w]["neu"], stat[l]["neu"]
    margin = abs(a - b)
    # 중반(절반 시점) 리드 기준 역전 여부
    mid = lead[len(lead)//2] if lead else 0
    mid_w = mid if w == 1 else -mid    # 승자 관점 중반 리드
    cfb = mid_w < 0                     # 중반에 지고 있었음 → 역전승
    parts = []
    if ws - ls >= 3:
        parts.append(f"탈취우위(+{ws-ls}칸 빼앗음,2배효과)")
    if wn - ln_ >= 3:
        parts.append(f"빈칸선점(+{wn-ln_})")
    if w == 1 and margin <= 4:
        parts.append("선공이점")
    if cfb:
        parts.append("역전승")
    if not parts:
        parts.append("근소복합우위")
    return " / ".join(parts)


def main():
    tot = {1: 0, 2: 0, 0: 0}
    agg = dict(stole_more=0, neu_more=0, cfb=0, games=0,
               wsteal=0, lsteal=0, wneu=0, lneu=0, margin=0)
    for d in DIRS:
        for path in sorted(glob.glob(os.path.join(d, "game_*.txt"))):
            rows, moves, _ = parse_game(path)
            stat, winner, a, b, lead = analyze(rows, moves)
            tot[winner] += 1
            rs = reason(stat, winner, a, b, lead)
            # ANALYSIS 줄 추가(중복 방지: 기존 ANALYSIS 제거 후)
            with open(path) as f: content = [l for l in f if not l.startswith("ANALYSIS")]
            with open(path, "w") as f:
                f.writelines(content)
                f.write(f"ANALYSIS winner=P{winner} score={a}-{b} reason={rs} "
                        f"steal(w/l)={stat[winner]['steal'] if winner else 0}/"
                        f"{stat[3-winner]['steal'] if winner else 0} "
                        f"neu(w/l)={stat[winner]['neu'] if winner else 0}/"
                        f"{stat[3-winner]['neu'] if winner else 0}\n")
            if winner:
                w, l = winner, 3-winner
                agg["games"] += 1
                agg["margin"] += abs(a-b)
                agg["wsteal"] += stat[w]["steal"]; agg["lsteal"] += stat[l]["steal"]
                agg["wneu"] += stat[w]["neu"]; agg["lneu"] += stat[l]["neu"]
                if stat[w]["steal"] > stat[l]["steal"]: agg["stole_more"] += 1
                if stat[w]["neu"] > stat[l]["neu"]: agg["neu_more"] += 1
                mid = lead[len(lead)//2] if lead else 0
                if (mid if w == 1 else -mid) < 0: agg["cfb"] += 1

    g = agg["games"]
    out = []
    out.append("===== selfplay 400게임 분석 =====")
    out.append(f"승자: FIRST {tot[1]}  SECOND {tot[2]}  무 {tot[0]}")
    out.append(f"선공(FIRST) 승률(무제외): {tot[1]/(tot[1]+tot[2])*100:.1f}%")
    out.append(f"평균 점수차(승패): {agg['margin']/g:.1f}")
    out.append("")
    out.append(f"승자 평균 탈취={agg['wsteal']/g:.1f}  패자 평균 탈취={agg['lsteal']/g:.1f}")
    out.append(f"승자 평균 빈칸={agg['wneu']/g:.1f}  패자 평균 빈칸={agg['lneu']/g:.1f}")
    out.append(f"승자가 '탈취를 더 많이' 한 게임: {agg['stole_more']/g*100:.1f}%")
    out.append(f"승자가 '빈칸을 더 많이' 먹은 게임: {agg['neu_more']/g*100:.1f}%")
    out.append(f"역전승(중반 열세→승리): {agg['cfb']/g*100:.1f}%")
    rep = "\n".join(out)
    print(rep)
    with open("analysis_summary.txt", "w") as f:
        f.write(rep + "\n")


if __name__ == "__main__":
    main()
