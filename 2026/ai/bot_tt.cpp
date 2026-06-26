// 탐색 강화 봇: bot_strong 의 평가/후보선별 그대로 + 트랜스포지션 테이블(TT) + TT 이동순서.
//   목적: 같은 평가로 같은 시간에 더 깊이 탐색 → 실시간에서 bot_strong 격파.
//   외부 파일 불필요(가중치 없음). 컴파일: g++ -std=c++17 -O2 -o bot_tt bot_tt.cpp
//   FIXED_DEPTH 환경변수 지원(학습/벤치 재현용).
#include <bits/stdc++.h>
using namespace std;

int FIXED_DEPTH = 0;

// ===== Zobrist & Transposition Table =====
static const int TTBITS = 21;
static const size_t TTSIZE = (size_t)1 << TTBITS;
static const size_t TTMASK = TTSIZE - 1;
struct TTEntry {
    uint64_t key = 0;
    long long value = 0;
    int16_t depth = -1;
    uint8_t flag = 0;            // 0 EXACT, 1 LOWER, 2 UPPER
    int8_t r1 = -1, c1 = -1, r2 = -1, c2 = -1;   // best move
};
vector<TTEntry> TT;
uint64_t ZOB[10][17][3];
uint64_t ZTURN, ZPASS;

void initZobrist() {
    mt19937_64 rng(0x9E3779B97F4A7C15ULL);
    for (int r = 0; r < 10; r++)
        for (int c = 0; c < 17; c++) {
            ZOB[r][c][0] = 0;               // 빈칸은 해시 기여 없음
            ZOB[r][c][1] = rng();
            ZOB[r][c][2] = rng();
        }
    ZTURN = rng();
    ZPASS = rng();
}

struct Move {
    int r1 = -1, c1 = -1, r2 = -1, c2 = -1;
    int area = 0, neutral = 0, stolen = 0, own = 0, gain = 0;
    long long orderScore = 0, searchScore = 0;
    bool isPass() const { return r1 < 0; }
};

struct Game {
    static constexpr int R = 10, C = 17;
    int val[R][C]{}; bool alive[R][C]{}; int owner[R][C]{};
    int me = 1, opp = 2; bool lastPass = false;
    uint64_t hash = 0;          // owner 구성의 Zobrist 해시(증분 갱신)

    mutable bool timeUp = false;
    mutable chrono::steady_clock::time_point deadline;
    mutable long long nodeCnt = 0;

    bool outOfTime() const {
        if (FIXED_DEPTH > 0) return false;
        if (timeUp) return true;
        if ((++nodeCnt & 255) == 0)     // 256노드마다 마감 확인
            if (chrono::steady_clock::now() >= deadline) timeUp = true;
        return timeUp;
    }
    void init(const vector<string>& rows) {
        memset(owner, 0, sizeof(owner)); lastPass = false; hash = 0;
        for (int r = 0; r < R; r++) for (int c = 0; c < C; c++) {
            val[r][c] = rows[r][c] - '0'; alive[r][c] = true;
        }
    }
    int diffScore(int player) const {
        int e = 3 - player, a = 0, b = 0;
        for (int r = 0; r < R; r++) for (int c = 0; c < C; c++) {
            if (owner[r][c] == player) a++; else if (owner[r][c] == e) b++;
        }
        return a - b;
    }
    int occupiedCount() const {
        int n = 0; for (int r = 0; r < R; r++) for (int c = 0; c < C; c++) if (owner[r][c]) n++; return n;
    }
    long long terminalScore(int player) const {
        int d = diffScore(player);
        if (d > 0) return 1000000000LL + 1000000LL * d;
        if (d == 0) return 0;
        return -1000000000LL + 1000000LL * d;
    }
    Move makeMove(int r1, int c1, int r2, int c2, int player) const {
        Move m; m.r1 = r1; m.c1 = c1; m.r2 = r2; m.c2 = c2;
        m.area = (r2 - r1 + 1) * (c2 - c1 + 1); int e = 3 - player;
        for (int r = r1; r <= r2; r++) for (int c = c1; c <= c2; c++) {
            if (owner[r][c] == 0) m.neutral++; else if (owner[r][c] == e) m.stolen++; else m.own++;
        }
        m.gain = m.neutral + 2 * m.stolen;
        m.orderScore = 120000LL*m.gain + 70000LL*m.stolen + 9000LL*m.neutral + 300LL*m.area - 2000LL*m.own;
        return m;
    }
    vector<Move> getCandidates(int player) const {
        int psV[R+1][C+1]{}, psA[R+1][C+1]{};
        for (int r = 0; r < R; r++) for (int c = 0; c < C; c++) {
            int v = alive[r][c]?val[r][c]:0, a = alive[r][c]?1:0;
            psV[r+1][c+1] = psV[r][c+1]+psV[r+1][c]-psV[r][c]+v;
            psA[r+1][c+1] = psA[r][c+1]+psA[r+1][c]-psA[r][c]+a;
        }
        auto rV=[&](int r1,int c1,int r2,int c2){return psV[r2+1][c2+1]-psV[r1][c2+1]-psV[r2+1][c1]+psV[r1][c1];};
        auto rA=[&](int r1,int c1,int r2,int c2){return psA[r2+1][c2+1]-psA[r1][c2+1]-psA[r2+1][c1]+psA[r1][c1];};
        vector<Move> res;
        for (int r1=0;r1<R;r1++) for (int r2=r1;r2<R;r2++) for (int c1=0;c1<C;c1++) for (int c2=c1;c2<C;c2++) {
            if (rV(r1,c1,r2,c2)!=10) continue;
            if (rA(r1,c1,r1,c2)==0||rA(r2,c1,r2,c2)==0||rA(r1,c1,r2,c1)==0||rA(r1,c2,r2,c2)==0) continue;
            res.push_back(makeMove(r1,c1,r2,c2,player));
        }
        sort(res.begin(),res.end(),[](const Move&a,const Move&b){
            if(a.orderScore!=b.orderScore)return a.orderScore>b.orderScore;
            if(a.area!=b.area)return a.area>b.area;
            if(a.r1!=b.r1)return a.r1<b.r1;
            if(a.c1!=b.c1)return a.c1<b.c1;
            if(a.r2!=b.r2)return a.r2<b.r2;
            return a.c2<b.c2;
        });
        return res;
    }
    int overlapAlive(const Move&a,const Move&b) const {
        if (a.isPass()||b.isPass()) return 0;
        int r1=max(a.r1,b.r1),c1=max(a.c1,b.c1),r2=min(a.r2,b.r2),c2=min(a.c2,b.c2);
        if (r1>r2||c1>c2) return 0;
        int cnt=0; for(int r=r1;r<=r2;r++)for(int c=c1;c<=c2;c++) if(alive[r][c])cnt++;
        return cnt;
    }
    void applyMove(const Move& m, int player) {
        if (m.isPass()) { lastPass = true; return; }
        for (int r = m.r1; r <= m.r2; r++) for (int c = m.c1; c <= m.c2; c++) {
            int old = owner[r][c];
            if (old != player) hash ^= ZOB[r][c][old] ^ ZOB[r][c][player];
            alive[r][c] = false; owner[r][c] = player;
        }
        lastPass = false;
    }
    void applyCoords(int r1,int c1,int r2,int c2,int player) {
        Move m; m.r1=r1;m.c1=c1;m.r2=r2;m.c2=c2; applyMove(m,player);
    }
    Move passMove() const { return Move{}; }
    long long topGainSum(const vector<Move>&v,int k) const {
        long long s=0; for(int i=0;i<min(k,(int)v.size());i++)s+=v[i].gain; return s;
    }
    long long topStealSum(const vector<Move>&v,int k) const {
        long long s=0; for(int i=0;i<min(k,(int)v.size());i++)s+=v[i].stolen; return s;
    }
    long long evaluate(int player) const {
        int e = 3 - player;
        vector<Move> mine = getCandidates(player), they = getCandidates(e);
        int diff = diffScore(player);
        long long myGain1 = mine.empty()?0:mine[0].gain, opGain1 = they.empty()?0:they[0].gain;
        long long myGain3 = topGainSum(mine,3), opGain3 = topGainSum(they,3);
        long long mySteal3 = topStealSum(mine,3), opSteal3 = topStealSum(they,3);
        long long s = 0;
        s += 120000LL*diff;
        s += 14000LL*myGain1; s += 7000LL*myGain3; s += 9000LL*mySteal3;
        s -= 23000LL*opGain1; s -= 15000LL*opGain3; s -= 16000LL*opSteal3;
        s += 100LL*((int)mine.size()-(int)they.size());
        return s;
    }
    vector<Move> pickMoves(int player, int beam, bool safeSort) const {
        vector<Move> all = getCandidates(player);
        if (all.empty()) return {};
        int e = 3 - player;
        vector<Move> enemyMoves = getCandidates(e);
        vector<Move> pool; set<tuple<int,int,int,int>> used;
        auto addMove=[&](const Move&m){auto k=make_tuple(m.r1,m.c1,m.r2,m.c2);if(used.insert(k).second)pool.push_back(m);};
        auto addTopBy=[&](auto cmp,int cnt){
            vector<Move> tmp=all; sort(tmp.begin(),tmp.end(),cmp);
            for(int i=0;i<min(cnt,(int)tmp.size());i++)addMove(tmp[i]);
        };
        if ((int)all.size() <= max(beam*2,32)) {
            for (const Move&m:all) addMove(m);
        } else {
            int wide = max(beam*3,45);
            for (int i=0;i<min(wide,(int)all.size());i++) addMove(all[i]);
            addTopBy([](const Move&a,const Move&b){
                if(a.stolen!=b.stolen)return a.stolen>b.stolen;
                if(a.gain!=b.gain)return a.gain>b.gain;
                return a.area>b.area;},beam);
            addTopBy([](const Move&a,const Move&b){
                if(a.area!=b.area)return a.area>b.area;
                if(a.gain!=b.gain)return a.gain>b.gain;
                return a.stolen>b.stolen;},beam);
        }
        if (!enemyMoves.empty()) {
            vector<pair<long long,int>> blockRank;
            int T = min(14,(int)enemyMoves.size());
            for (int i=0;i<(int)all.size();i++) {
                long long bs=0;
                for (int j=0;j<T;j++) {
                    int ov=overlapAlive(all[i],enemyMoves[j]); if(ov==0)continue;
                    bs+=1LL*ov*(1000LL*enemyMoves[j].gain+950LL*enemyMoves[j].stolen+130LL*enemyMoves[j].area);
                }
                bs += 3500LL*all[i].gain + 5500LL*all[i].stolen;
                blockRank.push_back({bs,i});
            }
            sort(blockRank.begin(),blockRank.end(),greater<>());
            for (int i=0;i<min(beam*2,(int)blockRank.size());i++) addMove(all[blockRank[i].second]);
        }
        if (safeSort) {
            for (Move& m : pool) {
                Game nxt=*this; nxt.applyMove(m,player);
                vector<Move> op=nxt.getCandidates(e);
                long long opGain1=op.empty()?0:op[0].gain;
                long long opGain3=nxt.topGainSum(op,3), opSteal3=nxt.topStealSum(op,3);
                m.searchScore = 150000LL*nxt.diffScore(player)+76000LL*m.gain+56000LL*m.stolen
                              + 12000LL*m.area - 115000LL*opGain1 - 52000LL*opGain3 - 58000LL*opSteal3;
            }
            sort(pool.begin(),pool.end(),[](const Move&a,const Move&b){
                if(a.searchScore!=b.searchScore)return a.searchScore>b.searchScore;
                return a.orderScore>b.orderScore;});
        } else {
            sort(pool.begin(),pool.end(),[](const Move&a,const Move&b){
                if(a.orderScore!=b.orderScore)return a.orderScore>b.orderScore;
                return a.area>b.area;});
        }
        if ((int)pool.size()>beam) pool.resize(beam);
        return pool;
    }

    long long negamax(Game state, int player, int depth, bool prevPass, int beam,
                      long long alpha, long long beta) const {
        long long alphaOrig = alpha;
        uint64_t tkey = state.hash ^ (player==2?ZTURN:0) ^ (prevPass?ZPASS:0);
        TTEntry& e = TT[tkey & TTMASK];
        bool hit = (e.key == tkey);
        if (hit && e.depth >= depth) {
            if (e.flag == 0) return e.value;
            else if (e.flag == 1) { if (e.value > alpha) alpha = e.value; }
            else { if (e.value < beta) beta = e.value; }
            if (alpha >= beta) return e.value;
        }
        if (depth <= 0 || outOfTime()) return state.evaluate(player);

        vector<Move> moves = state.pickMoves(player, beam, false);
        if (state.occupiedCount() > 0) moves.push_back(state.passMove());
        if (moves.empty()) return state.evaluate(player);

        // TT 최선수를 맨 앞으로(이동순서 개선)
        if (hit && e.r1 >= 0) {
            for (size_t i = 0; i < moves.size(); i++)
                if (moves[i].r1==e.r1&&moves[i].c1==e.c1&&moves[i].r2==e.r2&&moves[i].c2==e.c2) {
                    swap(moves[0], moves[i]); break;
                }
        }

        long long best = LLONG_MIN/4; Move bestMove = state.passMove();
        int nextBeam = max(5, beam - 2);
        for (const Move& m : moves) {
            long long sc;
            if (m.isPass()) {
                if (prevPass) sc = state.terminalScore(player);
                else { Game nx=state; nx.applyMove(m,player); sc=-negamax(nx,3-player,depth-1,true,nextBeam,-beta,-alpha); }
            } else { Game nx=state; nx.applyMove(m,player); sc=-negamax(nx,3-player,depth-1,false,nextBeam,-beta,-alpha); }
            if (sc > best) { best = sc; bestMove = m; }
            if (sc > alpha) alpha = sc;
            if (alpha >= beta) break;
            if (timeUp) break;
        }

        if (!timeUp && (!hit || e.depth <= depth)) {   // TT 저장(깊이 우선 교체)
            e.key = tkey; e.value = best; e.depth = (int16_t)depth;
            e.flag = (best <= alphaOrig) ? 2 : (best >= beta) ? 1 : 0;
            e.r1 = (int8_t)bestMove.r1; e.c1 = (int8_t)bestMove.c1;
            e.r2 = (int8_t)bestMove.r2; e.c2 = (int8_t)bestMove.c2;
        }
        return best;
    }

    Move chooseMove(int timeLeft) {
        auto t0 = chrono::steady_clock::now();
        long long budget = timeLeft / 5;   // bot_strong(/8)보다 더 써서 더 깊이(정확한 평가라 깊이=강함)
        budget = min<long long>(budget, (long long)timeLeft - 500);
        budget = max<long long>(budget, 20);
        deadline = t0 + chrono::milliseconds(budget);
        timeUp = false; nodeCnt = 0;

        int myCnt = (int)getCandidates(me).size();
        int beam = 16; if (myCnt<=26) beam=14; if (myCnt<=18) beam=12; if (myCnt<=10) beam=10;
        vector<Move> root = pickMoves(me, beam, true);
        if (occupiedCount() > 0) root.push_back(passMove());
        if (root.empty()) return passMove();

        Move best = root[0];
        int maxDepth = FIXED_DEPTH > 0 ? FIXED_DEPTH : 30;   // TT로 깊이 확장
        int startDepth = FIXED_DEPTH > 0 ? FIXED_DEPTH : 1;
        for (int depth = startDepth; depth <= maxDepth; depth++) {
            Move lb = root[0]; long long bs = LLONG_MIN/4; bool done = true;
            for (const Move& m : root) {
                long long sc;
                if (m.isPass()) {
                    if (lastPass) sc = terminalScore(me);
                    else { Game nx=*this; nx.applyMove(m,me); sc=-negamax(nx,opp,depth-1,true,max(5,beam-2),LLONG_MIN/4,LLONG_MAX/4); }
                } else { Game nx=*this; nx.applyMove(m,me); sc=-negamax(nx,opp,depth-1,false,max(5,beam-2),LLONG_MIN/4,LLONG_MAX/4); }
                if (timeUp) { done = false; break; }
                if (sc > bs || (sc == bs && m.orderScore > lb.orderScore)) { bs = sc; lb = m; }
            }
            if (!done) break;
            best = lb;
            if (FIXED_DEPTH > 0) break;
            auto it = find_if(root.begin(), root.end(), [&](const Move& x){
                return x.r1==best.r1&&x.c1==best.c1&&x.r2==best.r2&&x.c2==best.c2;});
            if (it != root.end()) rotate(root.begin(), it, it+1);
            if (chrono::steady_clock::now() >= deadline) break;
        }
        return best;
    }
};

int main() {
    ios::sync_with_stdio(false); cin.tie(nullptr);
    if (const char* d = getenv("FIXED_DEPTH")) FIXED_DEPTH = atoi(d);
    if (const char* t = getenv("TTFIX")) FIXED_DEPTH = atoi(t);   // bot_tt 전용 깊이(진단용)
    initZobrist();
    TT.assign(TTSIZE, TTEntry{});

    Game game; string cmd;
    while (cin >> cmd) {
        if (cmd == "READY") {
            string o; cin >> o;
            if (o == "FIRST") { game.me=1; game.opp=2; } else { game.me=2; game.opp=1; }
            cout << "OK" << endl;
        } else if (cmd == "INIT") {
            vector<string> rows(10); for (int i=0;i<10;i++) cin >> rows[i];
            game.init(rows);
            fill(TT.begin(), TT.end(), TTEntry{});   // 새 게임이면 TT 초기화(val 변경 대비)
        } else if (cmd == "TIME") {
            int t1,t2; cin >> t1 >> t2;
            Move m = game.chooseMove(t1); game.applyMove(m, game.me);
            cout << m.r1<<' '<<m.c1<<' '<<m.r2<<' '<<m.c2 << endl;
        } else if (cmd == "OPP") {
            int r1,c1,r2,c2,t; cin >> r1>>c1>>r2>>c2>>t;
            game.applyCoords(r1,c1,r2,c2,game.opp);
        } else if (cmd == "FINISH") break;
    }
    return 0;
}
