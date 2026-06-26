#include <bits/stdc++.h>
using namespace std;

/*
 * Mushroom-field territory game engine.
 *
 * Design goals (to beat 18913.cpp / 19931.cpp which share the same base):
 *  1. CHEAP TREE NODES.  Inside the search tree we only generate our own
 *     candidate moves and order them by a static orderScore; we do NOT
 *     regenerate the opponent's full candidate list or run overlap/block
 *     ranking at every node (the colleagues' pickMoves(false) does, which is
 *     expensive).  Cheaper nodes -> we search noticeably deeper in the same
 *     wall-clock budget.
 *  2. EXACT ENDGAME.  When the branching factor is small we drop the beam and
 *     search the (near) full tree, so close endgames are played precisely.
 *  3. ADAPTIVE, SAFE TIME.  Budget scales with the position while always
 *     keeping a hard reserve so we never TLE.
 *
 * Rules implemented (matching the proven colleague code / official tool):
 *  - Board 10x17, values 1..9.
 *  - A move is an axis-aligned rectangle whose ALIVE-cell value sum == 10,
 *    and each of the 4 border rows/cols contains >= 1 alive cell.
 *  - Applying the move marks every cell in the rectangle as owned by the
 *    player (stealing opponent cells) and removes the mushrooms (alive=false).
 *  - Pass = -1 -1 -1 -1.  Two consecutive passes end the game.
 *  - Winner = more owned cells at the end.
 */

struct Move {
    int r1 = -1, c1 = -1, r2 = -1, c2 = -1;
    int area = 0;
    int neutral = 0;
    int stolen = 0;
    int own = 0;
    int gain = 0;
    long long orderScore = 0;
    long long searchScore = 0;

    bool isPass() const { return r1 < 0; }
};

struct Game {
    static constexpr int R = 10;
    static constexpr int C = 17;

    int val[R][C]{};
    bool alive[R][C]{};
    int owner[R][C]{}; // 0 none, 1 FIRST, 2 SECOND

    int me = 1;
    int opp = 2;
    bool lastPass = false;

    // time control (shared via the root object's mutable members)
    mutable bool timeUp = false;
    mutable chrono::steady_clock::time_point deadline;
    mutable long long nodeCnt = 0;

    bool outOfTime() const {
        if (timeUp) return true;
        if ((++nodeCnt & 1023) == 0) {
            if (chrono::steady_clock::now() >= deadline) timeUp = true;
        }
        return timeUp;
    }

    void init(const vector<string>& rows) {
        memset(owner, 0, sizeof(owner));
        lastPass = false;
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++) {
                val[r][c] = rows[r][c] - '0';
                alive[r][c] = true;
            }
    }

    int diffScore(int player) const {
        int enemy = 3 - player, mine = 0, theirs = 0;
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++) {
                if (owner[r][c] == player) mine++;
                else if (owner[r][c] == enemy) theirs++;
            }
        return mine - theirs;
    }

    int occupiedCount() const {
        int cnt = 0;
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++)
                if (owner[r][c] != 0) cnt++;
        return cnt;
    }

    int aliveCount() const {
        int cnt = 0;
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++)
                if (alive[r][c]) cnt++;
        return cnt;
    }

    long long terminalScore(int player) const {
        int d = diffScore(player);
        if (d > 0) return 1000000000LL + 1000000LL * d;
        if (d == 0) return 0;
        return -1000000000LL + 1000000LL * d;
    }

    Move makeMove(int r1, int c1, int r2, int c2, int player) const {
        Move m;
        m.r1 = r1; m.c1 = c1; m.r2 = r2; m.c2 = c2;
        m.area = (r2 - r1 + 1) * (c2 - c1 + 1);
        int enemy = 3 - player;
        for (int r = r1; r <= r2; r++)
            for (int c = c1; c <= c2; c++) {
                if (owner[r][c] == 0) m.neutral++;
                else if (owner[r][c] == enemy) m.stolen++;
                else m.own++;
            }
        // score-diff change: neutral +1, steal +2 (them -1, me +1)
        m.gain = m.neutral + 2 * m.stolen;
        m.orderScore =
            120000LL * m.gain +
             70000LL * m.stolen +
              9000LL * m.neutral +
               300LL * m.area -
              2000LL * m.own;
        return m;
    }

    // Generate every legal move, sorted by orderScore (desc).
    vector<Move> getCandidates(int player) const {
        int psVal[R + 1][C + 1]{};
        int psAlive[R + 1][C + 1]{};
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++) {
                int v = alive[r][c] ? val[r][c] : 0;
                int a = alive[r][c] ? 1 : 0;
                psVal[r + 1][c + 1] = psVal[r][c + 1] + psVal[r + 1][c] - psVal[r][c] + v;
                psAlive[r + 1][c + 1] = psAlive[r][c + 1] + psAlive[r + 1][c] - psAlive[r][c] + a;
            }
        auto rectVal = [&](int r1, int c1, int r2, int c2) {
            return psVal[r2 + 1][c2 + 1] - psVal[r1][c2 + 1] - psVal[r2 + 1][c1] + psVal[r1][c1];
        };
        auto rectAlive = [&](int r1, int c1, int r2, int c2) {
            return psAlive[r2 + 1][c2 + 1] - psAlive[r1][c2 + 1] - psAlive[r2 + 1][c1] + psAlive[r1][c1];
        };

        vector<Move> res;
        res.reserve(256);
        for (int r1 = 0; r1 < R; r1++)
            for (int r2 = r1; r2 < R; r2++)
                for (int c1 = 0; c1 < C; c1++) {
                    for (int c2 = c1; c2 < C; c2++) {
                        int s = rectVal(r1, c1, r2, c2);
                        if (s != 10) {
                            if (s > 10) break; // adding columns only increases the alive sum
                            continue;
                        }
                        bool top = rectAlive(r1, c1, r1, c2) > 0;
                        bool bottom = rectAlive(r2, c1, r2, c2) > 0;
                        bool left = rectAlive(r1, c1, r2, c1) > 0;
                        bool right = rectAlive(r1, c2, r2, c2) > 0;
                        if (!(top && bottom && left && right)) continue;
                        res.push_back(makeMove(r1, c1, r2, c2, player));
                    }
                }

        sort(res.begin(), res.end(), [](const Move& a, const Move& b) {
            if (a.orderScore != b.orderScore) return a.orderScore > b.orderScore;
            if (a.area != b.area) return a.area > b.area;
            if (a.r1 != b.r1) return a.r1 < b.r1;
            if (a.c1 != b.c1) return a.c1 < b.c1;
            if (a.r2 != b.r2) return a.r2 < b.r2;
            return a.c2 < b.c2;
        });
        return res;
    }

    int overlapAlive(const Move& a, const Move& b) const {
        if (a.isPass() || b.isPass()) return 0;
        int r1 = max(a.r1, b.r1), c1 = max(a.c1, b.c1);
        int r2 = min(a.r2, b.r2), c2 = min(a.c2, b.c2);
        if (r1 > r2 || c1 > c2) return 0;
        int cnt = 0;
        for (int r = r1; r <= r2; r++)
            for (int c = c1; c <= c2; c++)
                if (alive[r][c]) cnt++;
        return cnt;
    }

    void applyMove(const Move& m, int player) {
        if (m.isPass()) { lastPass = true; return; }
        for (int r = m.r1; r <= m.r2; r++)
            for (int c = m.c1; c <= m.c2; c++) {
                alive[r][c] = false;
                owner[r][c] = player;
            }
        lastPass = false;
    }

    void applyCoords(int r1, int c1, int r2, int c2, int player) {
        Move m; m.r1 = r1; m.c1 = c1; m.r2 = r2; m.c2 = c2;
        applyMove(m, player);
    }

    Move passMove() const { return Move{}; }

    long long topGainSum(const vector<Move>& v, int k) const {
        long long s = 0;
        for (int i = 0; i < min(k, (int)v.size()); i++) s += v[i].gain;
        return s;
    }
    long long topStealSum(const vector<Move>& v, int k) const {
        long long s = 0;
        for (int i = 0; i < min(k, (int)v.size()); i++) s += v[i].stolen;
        return s;
    }

    // Leaf evaluation from `player`'s perspective.
    long long evaluate(int player) const {
        int enemy = 3 - player;
        vector<Move> mine = getCandidates(player);
        vector<Move> theirs = getCandidates(enemy);

        int diff = diffScore(player);
        long long myGain1 = mine.empty() ? 0 : mine[0].gain;
        long long opGain1 = theirs.empty() ? 0 : theirs[0].gain;
        long long myGain3 = topGainSum(mine, 3);
        long long opGain3 = topGainSum(theirs, 3);
        long long mySteal3 = topStealSum(mine, 3);
        long long opSteal3 = topStealSum(theirs, 3);

        long long score = 0;
        score += 120000LL * diff;
        // my opportunities
        score += 15000LL * myGain1;
        score +=  7000LL * myGain3;
        score +=  9000LL * mySteal3;
        // opponent's counter-threats (weighted a touch heavier -> safer play)
        score -= 24000LL * opGain1;
        score -= 15000LL * opGain3;
        score -= 17000LL * opSteal3;
        // mobility
        score += 120LL * ((int)mine.size() - (int)theirs.size());
        return score;
    }

    // ---- Tree move generation: CHEAP.  Only our own candidates, beam-limited.
    vector<Move> treeMoves(int player, int beam) const {
        vector<Move> all = getCandidates(player); // already orderScore-sorted
        if ((int)all.size() > beam) all.resize(beam);
        return all;
    }

    // ---- Root move generation: richer (block-ranking + one-ply safe sort).
    vector<Move> rootMovesGen(int player, int beam) const {
        vector<Move> all = getCandidates(player);
        if (all.empty()) return {};

        int enemy = 3 - player;
        vector<Move> enemyMoves = getCandidates(enemy);

        vector<Move> pool;
        set<tuple<int,int,int,int>> used;
        auto addMove = [&](const Move& m) {
            auto key = make_tuple(m.r1, m.c1, m.r2, m.c2);
            if (used.insert(key).second) pool.push_back(m);
        };
        auto addTopBy = [&](auto cmp, int cnt) {
            vector<Move> tmp = all;
            sort(tmp.begin(), tmp.end(), cmp);
            for (int i = 0; i < min(cnt, (int)tmp.size()); i++) addMove(tmp[i]);
        };

        if ((int)all.size() <= max(beam * 2, 40)) {
            for (const Move& m : all) addMove(m);
        } else {
            int wide = max(beam * 3, 50);
            for (int i = 0; i < min(wide, (int)all.size()); i++) addMove(all[i]);
            addTopBy([](const Move& a, const Move& b) {
                if (a.stolen != b.stolen) return a.stolen > b.stolen;
                if (a.gain != b.gain) return a.gain > b.gain;
                return a.area > b.area;
            }, beam);
            addTopBy([](const Move& a, const Move& b) {
                if (a.area != b.area) return a.area > b.area;
                if (a.gain != b.gain) return a.gain > b.gain;
                return a.stolen > b.stolen;
            }, beam);
        }

        // also consider moves that cut the opponent's strongest replies
        if (!enemyMoves.empty()) {
            vector<pair<long long,int>> blockRank;
            int T = min(16, (int)enemyMoves.size());
            for (int i = 0; i < (int)all.size(); i++) {
                long long bs = 0;
                for (int j = 0; j < T; j++) {
                    int ov = overlapAlive(all[i], enemyMoves[j]);
                    if (!ov) continue;
                    bs += 1LL * ov * (1000LL * enemyMoves[j].gain +
                                       950LL * enemyMoves[j].stolen +
                                       130LL * enemyMoves[j].area);
                }
                bs += 3500LL * all[i].gain + 5500LL * all[i].stolen;
                blockRank.push_back({bs, i});
            }
            sort(blockRank.begin(), blockRank.end(), greater<>());
            for (int i = 0; i < min(beam * 2, (int)blockRank.size()); i++)
                addMove(all[blockRank[i].second]);
        }

        // one-ply safe scoring of the pool
        for (Move& m : pool) {
            Game nxt = *this;
            nxt.applyMove(m, player);
            vector<Move> op = nxt.getCandidates(enemy);
            long long opGain1 = op.empty() ? 0 : op[0].gain;
            long long opGain3 = nxt.topGainSum(op, 3);
            long long opSteal3 = nxt.topStealSum(op, 3);
            m.searchScore =
                150000LL * nxt.diffScore(player) +
                 76000LL * m.gain +
                 56000LL * m.stolen +
                 12000LL * m.area -
                115000LL * opGain1 -
                 52000LL * opGain3 -
                 58000LL * opSteal3;
        }
        sort(pool.begin(), pool.end(), [](const Move& a, const Move& b) {
            if (a.searchScore != b.searchScore) return a.searchScore > b.searchScore;
            return a.orderScore > b.orderScore;
        });
        if ((int)pool.size() > beam) pool.resize(beam);
        return pool;
    }

    long long negamax(Game state, int player, int depth, bool prevPass,
                      int beam, long long alpha, long long beta) const {
        if (depth <= 0 || outOfTime())
            return state.evaluate(player);

        vector<Move> moves = state.treeMoves(player, beam);

        // PASS is always a legal candidate (except the very first move of game).
        bool canPass = state.occupiedCount() > 0;
        if (canPass) moves.push_back(state.passMove());

        long long best = LLONG_MIN / 4;
        int nextBeam = max(6, beam - 1);

        for (const Move& m : moves) {
            long long sc;
            if (m.isPass()) {
                if (prevPass) {
                    sc = state.terminalScore(player);
                } else {
                    Game nxt = state;
                    nxt.applyMove(m, player);
                    sc = -negamax(nxt, 3 - player, depth - 1, true, nextBeam, -beta, -alpha);
                }
            } else {
                Game nxt = state;
                nxt.applyMove(m, player);
                sc = -negamax(nxt, 3 - player, depth - 1, false, nextBeam, -beta, -alpha);
            }
            best = max(best, sc);
            alpha = max(alpha, sc);
            if (alpha >= beta) break;
            if (timeUp) break;
        }
        return best;
    }

    bool sameMove(const Move& a, const Move& b) const {
        return a.r1 == b.r1 && a.c1 == b.c1 && a.r2 == b.r2 && a.c2 == b.c2;
    }

    // Symmetric defensive guard (applies to BOTH colors).
    // Among root moves the deep search rated within a tiny margin of the chosen
    // best (i.e. effectively equal), prefer the one that minimizes the
    // opponent's immediate counter, without conceding our current territory.
    // It never overrides decisive/terminal-valued lines or sharp tactical
    // positions, so it can only break near-ties toward safer play.
    Move applySymmetricGuard(const vector<Move>& rootMoves, const Move& cur,
                             const map<tuple<int,int,int,int>, long long>& sc) const {
        if (cur.isPass()) return cur;
        if (occupiedCount() < 16) return cur;   // only once the board interacts

        vector<Move> myNow = getCandidates(me);
        vector<Move> opNow = getCandidates(opp);
        int myTopGain  = myNow.empty() ? 0 : myNow[0].gain;
        int myTopSteal = myNow.empty() ? 0 : myNow[0].stolen;
        int opTopGain  = opNow.empty() ? 0 : opNow[0].gain;
        int opTopSteal = opNow.empty() ? 0 : opNow[0].stolen;
        // quiet gate: leave sharp tactical positions to the search
        if (myTopGain >= 6 || opTopGain >= 6 || myTopSteal >= 2 || opTopSteal >= 2)
            return cur;

        auto key = [&](const Move& m) { return make_tuple(m.r1, m.c1, m.r2, m.c2); };
        auto it = sc.find(key(cur));
        if (it == sc.end()) return cur;
        long long curScore = it->second;
        if (llabs(curScore) >= 500000000LL) return cur; // decisive line: never touch

        // opponent's best immediate reply after a move (lower = safer)
        auto defMetric = [&](const Move& m, int& afterDiff) -> long long {
            Game nxt = *this;
            nxt.applyMove(m, me);
            afterDiff = nxt.diffScore(me);
            vector<Move> op = nxt.getCandidates(opp);
            long long og = op.empty() ? 0 : op[0].gain;
            long long os = op.empty() ? 0 : op[0].stolen;
            return 1000LL * og + 1400LL * os; // steal weighted a touch heavier
        };

        int curAfter;
        long long curDef = defMetric(cur, curAfter);

        Move guard = cur;
        long long bestDef = curDef;
        int bestAfter = curAfter;
        long long bestGScore = curScore;

        const long long MARGIN = 60000; // ~0.5 score-diff point: only near-equal evals
        for (const Move& m : rootMoves) {
            if (m.isPass() || sameMove(m, cur)) continue;
            auto jt = sc.find(key(m));
            if (jt == sc.end()) continue;
            long long ms = jt->second;
            if (ms < curScore - MARGIN) continue;     // must be ~as good per deep search

            int af;
            long long d = defMetric(m, af);
            if (af < curAfter) continue;              // never concede current territory

            if (d < bestDef ||
                (d == bestDef && (af > bestAfter ||
                 (af == bestAfter && ms > bestGScore)))) {
                bestDef = d; bestAfter = af; bestGScore = ms; guard = m;
            }
        }

        // switch only if it genuinely reduces the opponent's counter
        if (!sameMove(guard, cur) && curDef - bestDef >= 1000) {
#ifdef DBG
            fprintf(stderr, "GUARD switch: def %lld->%lld\n", curDef, bestDef);
#endif
            return guard;
        }
        return cur;
    }

    Move chooseMove(int timeLeft) {
        auto startT = chrono::steady_clock::now();

        // --- adaptive, safe time budget -------------------------------------
        // A full game lasts ~15 of MY moves. Spend most of the clock across
        // those moves (converting time -> search depth) while always keeping a
        // hard reserve so we can never TLE.
        int alive = aliveCount();
        int estMyTurns = alive / 13;            // rough remaining MY turns
        int divisor = max(5, min(estMyTurns, 13));
        long long budget = (long long)timeLeft / divisor;
        budget = min(budget, 1800LL);                    // single-move ceiling
        budget = min(budget, (long long)timeLeft - 300); // hard reserve -> no TLE
        budget = max(budget, 25LL);
        deadline = startT + chrono::milliseconds(budget);
        timeUp = false;
        nodeCnt = 0;

        int myCnt = (int)getCandidates(me).size();

        // adaptive beam: wide / near-exact when branching is small
        int beam;
        if (myCnt <= 16) beam = myCnt;       // exact endgame
        else if (myCnt <= 28) beam = 20;
        else if (myCnt <= 45) beam = 16;
        else beam = 14;

        vector<Move> rootMoves = rootMovesGen(me, beam);
        if (occupiedCount() > 0) rootMoves.push_back(passMove());
        if (rootMoves.empty()) return passMove();

        Move best = rootMoves[0];

        // deep score of each root move from the last fully completed depth
        map<tuple<int,int,int,int>, long long> finalScore;

        for (int depth = 1; depth <= 24; depth++) {
            Move localBest = rootMoves[0];
            long long bestScore = LLONG_MIN / 4;
            bool completed = true;
            int childBeam = max(6, beam - 1);
            map<tuple<int,int,int,int>, long long> depthScore;

            for (const Move& m : rootMoves) {
                long long sc;
                if (m.isPass()) {
                    if (lastPass) {
                        sc = terminalScore(me);
                    } else {
                        Game nxt = *this;
                        nxt.applyMove(m, me);
                        sc = -negamax(nxt, opp, depth - 1, true, childBeam,
                                      LLONG_MIN / 4, LLONG_MAX / 4);
                    }
                } else {
                    Game nxt = *this;
                    nxt.applyMove(m, me);
                    sc = -negamax(nxt, opp, depth - 1, false, childBeam,
                                  LLONG_MIN / 4, LLONG_MAX / 4);
                }

                if (timeUp) { completed = false; break; }

                depthScore[make_tuple(m.r1, m.c1, m.r2, m.c2)] = sc;

                if (sc > bestScore ||
                    (sc == bestScore && m.searchScore > localBest.searchScore)) {
                    bestScore = sc;
                    localBest = m;
                }
            }

            if (!completed) break;     // discard unfinished depth
            best = localBest;
            finalScore = depthScore;

            // move PV to front for stronger alpha-beta cuts next depth
            auto it = find_if(rootMoves.begin(), rootMoves.end(), [&](const Move& x) {
                return x.r1 == best.r1 && x.c1 == best.c1 &&
                       x.r2 == best.r2 && x.c2 == best.c2;
            });
            if (it != rootMoves.end()) rotate(rootMoves.begin(), it, it + 1);

            if (chrono::steady_clock::now() >= deadline) break;
#ifdef DBG
            lastDepth = depth;
#endif
        }

        best = applySymmetricGuard(rootMoves, best, finalScore);
#ifdef DBG
        fprintf(stderr, "me=%d alive=%d cand=%d beam=%d budget=%lldms depth=%d\n",
                me, alive, myCnt, beam, budget, lastDepth);
#endif
        return best;
    }
#ifdef DBG
    mutable int lastDepth = 0;
#endif
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    Game game;
    string cmd;
    while (cin >> cmd) {
        if (cmd == "READY") {
            string order; cin >> order;
            if (order == "FIRST") { game.me = 1; game.opp = 2; }
            else { game.me = 2; game.opp = 1; }
            cout << "OK" << endl;
        } else if (cmd == "INIT") {
            vector<string> rows(10);
            for (int i = 0; i < 10; i++) cin >> rows[i];
            game.init(rows);
        } else if (cmd == "TIME") {
            int t1, t2; cin >> t1 >> t2;
            Move m = game.chooseMove(t1);
            game.applyMove(m, game.me);
            cout << m.r1 << ' ' << m.c1 << ' ' << m.r2 << ' ' << m.c2 << endl;
        } else if (cmd == "OPP") {
            int r1, c1, r2, c2, t; cin >> r1 >> c1 >> r2 >> c2 >> t;
            game.applyCoords(r1, c1, r2, c2, game.opp);
        } else if (cmd == "FINISH") {
            break;
        }
    }
    return 0;
}
