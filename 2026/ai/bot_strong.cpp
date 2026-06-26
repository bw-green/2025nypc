// 손튜닝 고정전략 봇 (학습 상대용).
//   - 가중치 파일을 읽지 않는 고정 봇. bot_param 의 학습 상대(opponent)로 사용.
//   - 환경변수 FIXED_DEPTH=k 가 있으면 iterative deepening 대신 정확히 k depth만 탐색
//     (학습 시 게임을 빠르고 재현가능하게 만들기 위함 — bot_param 과 동일 규약).
#include <bits/stdc++.h>
using namespace std;

int FIXED_DEPTH = 0;

struct Move {
    int r1 = -1, c1 = -1, r2 = -1, c2 = -1;
    int area = 0;
    int neutral = 0;
    int stolen = 0;
    int own = 0;
    int gain = 0;
    long long orderScore = 0;
    long long searchScore = 0;

    bool isPass() const {
        return r1 < 0;
    }
};

struct Game {
    static constexpr int R = 10;
    static constexpr int C = 17;

    int val[R][C]{};
    bool alive[R][C]{};
    int owner[R][C]{};
    // 0: none, 1: FIRST, 2: SECOND

    int me = 1;
    int opp = 2;
    bool lastPass = false;

    // 시간 제어 (루트 this의 멤버를 공유)
    mutable bool timeUp = false;
    mutable chrono::steady_clock::time_point deadline;
    mutable long long nodeCnt = 0;

    bool outOfTime() const {
        if (FIXED_DEPTH > 0) return false;
        if (timeUp) return true;
        if ((++nodeCnt & 2047) == 0) {
            if (chrono::steady_clock::now() >= deadline) timeUp = true;
        }
        return timeUp;
    }

    void init(const vector<string>& rows) {
        memset(owner, 0, sizeof(owner));
        lastPass = false;

        for (int r = 0; r < R; r++) {
            for (int c = 0; c < C; c++) {
                val[r][c] = rows[r][c] - '0';
                alive[r][c] = true;
            }
        }
    }

    int diffScore(int player) const {
        int enemy = 3 - player;
        int mine = 0;
        int theirs = 0;

        for (int r = 0; r < R; r++) {
            for (int c = 0; c < C; c++) {
                if (owner[r][c] == player) mine++;
                else if (owner[r][c] == enemy) theirs++;
            }
        }

        return mine - theirs;
    }

    int occupiedCount() const {
        int cnt = 0;

        for (int r = 0; r < R; r++) {
            for (int c = 0; c < C; c++) {
                if (owner[r][c] != 0) cnt++;
            }
        }

        return cnt;
    }

    long long terminalScore(int player) const {
        int d = diffScore(player);

        if (d > 0) {
            return 1000000000LL + 1000000LL * d;
        }

        if (d == 0) {
            // draw는 0점.
            // 초반 0:0에서 패스를 고르는 문제 방지.
            return 0;
        }

        return -1000000000LL + 1000000LL * d;
    }

    Move makeMove(int r1, int c1, int r2, int c2, int player) const {
        Move m;
        m.r1 = r1;
        m.c1 = c1;
        m.r2 = r2;
        m.c2 = c2;
        m.area = (r2 - r1 + 1) * (c2 - c1 + 1);

        int enemy = 3 - player;

        for (int r = r1; r <= r2; r++) {
            for (int c = c1; c <= c2; c++) {
                if (owner[r][c] == 0) m.neutral++;
                else if (owner[r][c] == enemy) m.stolen++;
                else m.own++;
            }
        }

        // 점수차 기준 변화량
        // 빈 칸 점령: +1
        // 상대 칸 탈취: 상대 -1, 나 +1 => +2
        m.gain = m.neutral + 2 * m.stolen;

        m.orderScore =
            120000LL * m.gain +
             70000LL * m.stolen +
              9000LL * m.neutral +
               300LL * m.area -
              2000LL * m.own;

        return m;
    }

    vector<Move> getCandidates(int player) const {
        int psVal[R + 1][C + 1]{};
        int psAlive[R + 1][C + 1]{};

        for (int r = 0; r < R; r++) {
            for (int c = 0; c < C; c++) {
                int v = alive[r][c] ? val[r][c] : 0;
                int a = alive[r][c] ? 1 : 0;

                psVal[r + 1][c + 1] =
                    psVal[r][c + 1] + psVal[r + 1][c] - psVal[r][c] + v;

                psAlive[r + 1][c + 1] =
                    psAlive[r][c + 1] + psAlive[r + 1][c] - psAlive[r][c] + a;
            }
        }

        auto rectVal = [&](int r1, int c1, int r2, int c2) {
            return psVal[r2 + 1][c2 + 1]
                 - psVal[r1][c2 + 1]
                 - psVal[r2 + 1][c1]
                 + psVal[r1][c1];
        };

        auto rectAlive = [&](int r1, int c1, int r2, int c2) {
            return psAlive[r2 + 1][c2 + 1]
                 - psAlive[r1][c2 + 1]
                 - psAlive[r2 + 1][c1]
                 + psAlive[r1][c1];
        };

        vector<Move> res;

        for (int r1 = 0; r1 < R; r1++) {
            for (int r2 = r1; r2 < R; r2++) {
                for (int c1 = 0; c1 < C; c1++) {
                    for (int c2 = c1; c2 < C; c2++) {
                        if (rectVal(r1, c1, r2, c2) != 10) continue;

                        bool top = rectAlive(r1, c1, r1, c2) > 0;
                        bool bottom = rectAlive(r2, c1, r2, c2) > 0;
                        bool left = rectAlive(r1, c1, r2, c1) > 0;
                        bool right = rectAlive(r1, c2, r2, c2) > 0;

                        if (!(top && bottom && left && right)) continue;

                        res.push_back(makeMove(r1, c1, r2, c2, player));
                    }
                }
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

        int r1 = max(a.r1, b.r1);
        int c1 = max(a.c1, b.c1);
        int r2 = min(a.r2, b.r2);
        int c2 = min(a.c2, b.c2);

        if (r1 > r2 || c1 > c2) return 0;

        int cnt = 0;
        for (int r = r1; r <= r2; r++) {
            for (int c = c1; c <= c2; c++) {
                if (alive[r][c]) cnt++;
            }
        }

        return cnt;
    }

    void applyMove(const Move& m, int player) {
        if (m.isPass()) {
            lastPass = true;
            return;
        }

        for (int r = m.r1; r <= m.r2; r++) {
            for (int c = m.c1; c <= m.c2; c++) {
                alive[r][c] = false;
                owner[r][c] = player;
            }
        }

        lastPass = false;
    }

    void applyCoords(int r1, int c1, int r2, int c2, int player) {
        Move m;
        m.r1 = r1;
        m.c1 = c1;
        m.r2 = r2;
        m.c2 = c2;
        applyMove(m, player);
    }

    Move passMove() const {
        return Move{};
    }

    long long topGainSum(const vector<Move>& v, int k) const {
        long long s = 0;
        for (int i = 0; i < min(k, (int)v.size()); i++) {
            s += v[i].gain;
        }
        return s;
    }

    long long topStealSum(const vector<Move>& v, int k) const {
        long long s = 0;
        for (int i = 0; i < min(k, (int)v.size()); i++) {
            s += v[i].stolen;
        }
        return s;
    }

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

        // 내 다음 기회
        score += 14000LL * myGain1;
        score +=  7000LL * myGain3;
        score +=  9000LL * mySteal3;

        // 상대의 다음 큰 반격 위험
        score -= 23000LL * opGain1;
        score -= 15000LL * opGain3;
        score -= 16000LL * opSteal3;

        // 선택지 수 차이
        score += 100LL * ((int)mine.size() - (int)theirs.size());

        return score;
    }

    vector<Move> pickMoves(int player, int beam, bool safeSort) const {
        vector<Move> all = getCandidates(player);
        if (all.empty()) return {};

        int enemy = 3 - player;
        vector<Move> enemyMoves = getCandidates(enemy);

        vector<Move> pool;
        set<tuple<int,int,int,int>> used;

        auto addMove = [&](const Move& m) {
            auto key = make_tuple(m.r1, m.c1, m.r2, m.c2);
            if (used.insert(key).second) {
                pool.push_back(m);
            }
        };

        auto addTopBy = [&](auto cmp, int cnt) {
            vector<Move> tmp = all;
            sort(tmp.begin(), tmp.end(), cmp);
            for (int i = 0; i < min(cnt, (int)tmp.size()); i++) {
                addMove(tmp[i]);
            }
        };

        if ((int)all.size() <= max(beam * 2, 32)) {
            for (const Move& m : all) addMove(m);
        } else {
            int wide = max(beam * 3, 45);

            for (int i = 0; i < min(wide, (int)all.size()); i++) {
                addMove(all[i]);
            }

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

        // 상대 위험 수를 끊는 후보도 섞음
        if (!enemyMoves.empty()) {
            vector<pair<long long, int>> blockRank;
            int T = min(14, (int)enemyMoves.size());

            for (int i = 0; i < (int)all.size(); i++) {
                long long blockScore = 0;

                for (int j = 0; j < T; j++) {
                    int ov = overlapAlive(all[i], enemyMoves[j]);
                    if (ov == 0) continue;

                    blockScore += 1LL * ov *
                        (1000LL * enemyMoves[j].gain +
                          950LL * enemyMoves[j].stolen +
                          130LL * enemyMoves[j].area);
                }

                blockScore += 3500LL * all[i].gain;
                blockScore += 5500LL * all[i].stolen;

                blockRank.push_back({blockScore, i});
            }

            sort(blockRank.begin(), blockRank.end(), greater<>());

            for (int i = 0; i < min(beam * 2, (int)blockRank.size()); i++) {
                addMove(all[blockRank[i].second]);
            }
        }

        if (safeSort) {
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
        } else {
            sort(pool.begin(), pool.end(), [](const Move& a, const Move& b) {
                if (a.orderScore != b.orderScore) return a.orderScore > b.orderScore;
                return a.area > b.area;
            });
        }

        if ((int)pool.size() > beam) pool.resize(beam);
        return pool;
    }

    long long negamax(
        Game state,
        int player,
        int depth,
        bool prevPass,
        int beam,
        long long alpha,
        long long beta
    ) const {
        if (depth <= 0 || outOfTime()) {
            return state.evaluate(player);
        }

        vector<Move> moves = state.pickMoves(player, beam, false);

        // 핵심:
        // PASS는 모든 depth에서 항상 합법 후보로 넣는다.
        // 특정 턴/점수 기준으로 예외 처리하지 않는다.
        if (state.occupiedCount() > 0) {
            moves.push_back(state.passMove());
        }

        long long best = LLONG_MIN / 4;
        int nextBeam = max(5, beam - 2);

        for (const Move& m : moves) {
            long long sc;

            if (m.isPass()) {
                if (prevPass) {
                    // 연속 패스면 게임 종료
                    sc = state.terminalScore(player);
                } else {
                    Game nxt = state;
                    nxt.applyMove(m, player);

                    sc = -negamax(
                        nxt,
                        3 - player,
                        depth - 1,
                        true,
                        nextBeam,
                        -beta,
                        -alpha
                    );
                }
            } else {
                Game nxt = state;
                nxt.applyMove(m, player);

                sc = -negamax(
                    nxt,
                    3 - player,
                    depth - 1,
                    false,
                    nextBeam,
                    -beta,
                    -alpha
                );
            }

            best = max(best, sc);
            alpha = max(alpha, sc);

            if (alpha >= beta) break;
            if (timeUp) break;   // 시간 초과면 즉시 중단 (상위에서 depth 폐기)
        }

        return best;
    }

    Move chooseMove(int timeLeft) {
        auto startT = chrono::steady_clock::now();

        // 이번 수 예산: 남은 시간을 적극적으로 사용 + 통신 마진
        long long budget = timeLeft / 8;
        budget = min<long long>(budget, (long long)timeLeft - 200);
        budget = max<long long>(budget, 50);
        deadline = startT + chrono::milliseconds(budget);
        timeUp = false;
        nodeCnt = 0;

        int myCnt = (int)getCandidates(me).size();
        int beam = 16;
        if (myCnt <= 26) beam = 14;
        if (myCnt <= 18) beam = 12;
        if (myCnt <= 10) beam = 10;

        vector<Move> rootMoves = pickMoves(me, beam, true);

        // root에서도 PASS를 후보에 넣는다.
        // 단, 아직 아무 칸도 점령되지 않은 0:0 첫 턴 상태에서는 제외한다.
        if (occupiedCount() > 0) {
            rootMoves.push_back(passMove());
        }

        if (rootMoves.empty()) {
            return passMove();
        }

        Move best = rootMoves[0];

        int maxDepth = FIXED_DEPTH > 0 ? FIXED_DEPTH : 14;
        int startDepth = FIXED_DEPTH > 0 ? FIXED_DEPTH : 1;

        // iterative deepening: deadline 전까지 depth를 올림
        for (int depth = startDepth; depth <= maxDepth; depth++) {
            Move localBest = rootMoves[0];
            long long bestScore = LLONG_MIN / 4;
            bool completed = true;

            for (const Move& m : rootMoves) {
                long long sc;

                if (m.isPass()) {
                    if (lastPass) {
                        // 내가 패스하면 연속 패스라 게임 종료
                        sc = terminalScore(me);
                    } else {
                        Game nxt = *this;
                        nxt.applyMove(m, me);

                        sc = -negamax(
                            nxt,
                            opp,
                            depth - 1,
                            true,
                            max(5, beam - 2),
                            LLONG_MIN / 4,
                            LLONG_MAX / 4
                        );
                    }
                } else {
                    Game nxt = *this;
                    nxt.applyMove(m, me);

                    sc = -negamax(
                        nxt,
                        opp,
                        depth - 1,
                        false,
                        max(5, beam - 2),
                        LLONG_MIN / 4,
                        LLONG_MAX / 4
                    );
                }

                if (timeUp) { completed = false; break; }  // 미완성 depth는 버림

                if (sc > bestScore ||
                    (sc == bestScore && m.orderScore > localBest.orderScore)) {
                    bestScore = sc;
                    localBest = m;
                }
            }

            if (!completed) break;   // 직전 depth 결과 유지
            best = localBest;

            if (FIXED_DEPTH > 0) break;   // 고정 깊이면 1회만

            // 다음 depth에서 PV를 맨 앞으로 → alpha-beta 컷 증가
            auto it = find_if(rootMoves.begin(), rootMoves.end(), [&](const Move& x) {
                return x.r1 == best.r1 && x.c1 == best.c1 &&
                       x.r2 == best.r2 && x.c2 == best.c2;
            });
            if (it != rootMoves.end()) rotate(rootMoves.begin(), it, it + 1);

            if (chrono::steady_clock::now() >= deadline) break;
        }

        return best;
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    if (const char* d = getenv("FIXED_DEPTH")) FIXED_DEPTH = atoi(d);

    Game game;
    string cmd;

    while (cin >> cmd) {
        if (cmd == "READY") {
            string order;
            cin >> order;

            if (order == "FIRST") {
                game.me = 1;
                game.opp = 2;
            } else {
                game.me = 2;
                game.opp = 1;
            }

            cout << "OK" << endl;
        }

        else if (cmd == "INIT") {
            vector<string> rows(10);
            for (int i = 0; i < 10; i++) {
                cin >> rows[i];
            }

            game.init(rows);
        }

        else if (cmd == "TIME") {
            int t1, t2;
            cin >> t1 >> t2;

            Move m = game.chooseMove(t1);
            game.applyMove(m, game.me);

            cout << m.r1 << ' ' << m.c1 << ' ' << m.r2 << ' ' << m.c2 << endl;
        }

        else if (cmd == "OPP") {
            int r1, c1, r2, c2, t;
            cin >> r1 >> c1 >> r2 >> c2 >> t;

            game.applyCoords(r1, c1, r2, c2, game.opp);
        }

        else if (cmd == "FINISH") {
            break;
        }
    }

    return 0;
}
