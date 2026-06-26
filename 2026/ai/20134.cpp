#include <algorithm>
#include <chrono>
#include <climits>
#include <cstring>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
using namespace std;

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

    // 점령 당한 영역 숫자 반환
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
        // 누적합 배열 만들기
        int psVal[R + 1][C + 1]{}; // 아직 살아있는 버섯 숫자의 합 - (0,0)~(r,c) 직사각형의 합
        int psAlive[R + 1][C + 1]{}; // 아직 살아있는 버섯 칸 개수 

        // Dynamic Programming으로 누적합 구하기
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

        // 사각형 값의 합 구하기
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

        // candidates 넣어놓는 곳
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

                        // makeMove에서 점수 만들어서 저장
                        res.push_back(makeMove(r1, c1, r2, c2, player));
                    }
                }
            }
        }

        // 정렬 기준 1. orderScore 큰 순 2.area 큰 순 3. r1 작은 순 4. c1 작은 순 5. r2 작은 순
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

    //겹치는 부분에 살아있는 버섯 갯수 구하기
    int overlapAlive(const Move& a, const Move& b) const {
        if (a.isPass() || b.isPass()) return 0;

        // 겹치는 사각형 좌표
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
        // 내 후보지들
        vector<Move> all = getCandidates(player);
        if (all.empty()) return {};

        int enemy = 3 - player;
        // 상대 후보지들
        vector<Move> enemyMoves = getCandidates(enemy);

        vector<Move> pool;// 최종적으로 탐색할 후보들을 넣어놓을 벡터
        set<tuple<int,int,int,int>> used; // 이미 넣은 좌표들을 저장하는 집합

        // 사용해보지 않은 좌표면 pool에 추가
        auto addMove = [&](const Move& m) {
            auto key = make_tuple(m.r1, m.c1, m.r2, m.c2);
            if (used.insert(key).second) { 
                pool.push_back(m);
            }
        };

        auto addTopBy = [&](auto cmp, int cnt) {
            vector<Move> tmp = all;
            int take = min(cnt, (int)tmp.size());

            if (take <= 0) return;

            partial_sort(tmp.begin(), tmp.begin() + take, tmp.end(), cmp);

            for (int i = 0; i < take; i++) {
                addMove(tmp[i]);
            }
        };

        if ((int)all.size() <= max(beam * 2, 32)) { // 선택지 < 선택할 갯수
            for (const Move& m : all) addMove(m);
        } else { // 선택지 > 선택할 갯수
            int wide = max(beam * 3, 45);

            // wide개를 addMove(addMove에서 사용한 좌표는 넣지 않기에 wide 갯수를 넣는것이 아니다.)
            // orderScore 기준으로 추가
            for (int i = 0; i < min(wide, (int)all.size()); i++) {
                addMove(all[i]);
            }

            //stolen gain area 순서 기준으로 추가
            addTopBy([](const Move& a, const Move& b) {
                if (a.stolen != b.stolen) return a.stolen > b.stolen;
                if (a.gain != b.gain) return a.gain > b.gain;
                return a.area > b.area;
            }, beam);

            //area gain stolen 순서 기준으로 추가
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
                    // 내가 갈 수 있는 모든 선택지와 상대 선택지 사이의 겹치는 살아있는 버섯 갯수
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

            int blockTake = min(beam * 2, (int)blockRank.size());

            if (blockTake > 0) {
                partial_sort(
                    blockRank.begin(),
                    blockRank.begin() + blockTake,
                    blockRank.end(),
                    greater<>()
                );
            }
        
            // 상대의 다음 획득 점수를 최대한 막을 수 있는 수들
            for (int i = 0; i < blockTake; i++) {
                addMove(all[blockRank[i].second]);
            }
        }

        auto trimPool = [&](auto cmp) {
            if ((int)pool.size() > beam) {
                partial_sort(pool.begin(), pool.begin() + beam, pool.end(), cmp);
                pool.resize(beam);
            } else {
                sort(pool.begin(), pool.end(), cmp);
            }
        };

        // 내가 둔 각각의 수 뒤에 상대의 이득 계산해서 다시 sort - 다만 depth는 1개
        if (safeSort) {
            for (Move& m : pool) {
                Game nxt = *this;
                nxt.applyMove(m, player);

                vector<Move> op = nxt.getCandidates(enemy);

                long long opGain1 = op.empty() ? 0 : op[0].gain; // 상대 최고 후보 1개 gain
                long long opGain3 = nxt.topGainSum(op, 3); // 상대 상위 3개 후보의 gain 합
                long long opSteal3 = nxt.topStealSum(op, 3); // 상대 상위 3개 후보의 stolen 합

                m.searchScore =
                    150000LL * nxt.diffScore(player) +
                     76000LL * m.gain +
                     56000LL * m.stolen +
                     12000LL * m.area -
                    115000LL * opGain1 -
                     52000LL * opGain3 -
                     58000LL * opSteal3;
            }

            trimPool([](const Move& a, const Move& b) {
                if (a.searchScore != b.searchScore) return a.searchScore > b.searchScore;
                return a.orderScore > b.orderScore;
            });
        } else { // 아니면 그냥 orderScore, area 기준으로 sort
            trimPool([](const Move& a, const Move& b) {
                if (a.orderScore != b.orderScore) return a.orderScore > b.orderScore;
                return a.area > b.area;
            });
        }

        return pool;
    }

    long long negamax(
        const Game& state,
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

    struct GreedyGuardInfo {
        Move move;
        bool valid = false;
        int afterDiff = 0;
        int oppGain = 0;
        int oppStolen = 0;
        int greedyDiff = 0;
        long long tieScore = LLONG_MIN / 4;
    };

    bool sameMove(const Move& a, const Move& b) const {
        return a.r1 == b.r1 && a.c1 == b.c1 &&
               a.r2 == b.r2 && a.c2 == b.c2;
    }

    GreedyGuardInfo greedyGuardInfo(const Move& m) const {
        GreedyGuardInfo info;
        if (m.isPass()) return info;

        Game nxt = *this;
        nxt.applyMove(m, me);

        vector<Move> op = nxt.getCandidates(opp);
        int opGain = op.empty() ? 0 : op[0].gain;
        int opStolen = op.empty() ? 0 : op[0].stolen;
        int afterDiff = nxt.diffScore(me);

        info.move = m;
        info.valid = true;
        info.afterDiff = afterDiff;
        info.oppGain = opGain;
        info.oppStolen = opStolen;
        info.greedyDiff = afterDiff - opGain;
        info.tieScore =
            1000000LL * info.greedyDiff +
              50000LL * info.afterDiff -
              70000LL * info.oppStolen +
               2000LL * m.gain +
               1000LL * m.stolen +
                       m.searchScore / 1000 +
                       m.orderScore / 100000;

        return info;
    }

    bool betterGreedyGuard(const GreedyGuardInfo& a, const GreedyGuardInfo& b) const {
        if (!b.valid) return a.valid;
        if (!a.valid) return false;
        if (a.greedyDiff != b.greedyDiff) return a.greedyDiff > b.greedyDiff;
        if (a.afterDiff != b.afterDiff) return a.afterDiff > b.afterDiff;
        if (a.oppStolen != b.oppStolen) return a.oppStolen < b.oppStolen;
        if (a.tieScore != b.tieScore) return a.tieScore > b.tieScore;
        return a.move.orderScore > b.move.orderScore;
    }

    Move applyGreedyResponseGuard(const vector<Move>& rootMoves, const Move& currentBest) const {
        if (me != 2 || occupiedCount() < 20 || currentBest.isPass()) {
            return currentBest;
        }

        vector<Move> myNow = getCandidates(me);
        vector<Move> opNow = getCandidates(opp);
        int myTopGain = myNow.empty() ? 0 : myNow[0].gain;
        int myTopSteal = myNow.empty() ? 0 : myNow[0].stolen;
        int opTopGain = opNow.empty() ? 0 : opNow[0].gain;
        int opTopSteal = opNow.empty() ? 0 : opNow[0].stolen;

        // Keep this guard for the medium defensive shape that killed case 8.
        // In attacking cases, large steal chains are normal; overriding the
        // search with a one-ply "safe" move makes the bot too passive.
        if (myTopGain >= 6 || opTopGain >= 6 ||
            myTopSteal >= 2 || opTopSteal >= 2) {
            return currentBest;
        }

        GreedyGuardInfo cur = greedyGuardInfo(currentBest);
        if (!cur.valid) return currentBest;

        // If the deeper search already chose an attacking move, do not replace
        // it with a one-ply safety move. This preserves steal/large-gain tempo
        // on random boards while keeping the quiet-position guard available.
        if (cur.move.gain >= 4 || cur.move.stolen >= 1 || cur.move.area >= 4) {
            return currentBest;
        }

        GreedyGuardInfo guard = cur;
        for (const Move& m : rootMoves) {
            if (m.isPass()) continue;

            GreedyGuardInfo cand = greedyGuardInfo(m);
            if (betterGreedyGuard(cand, guard)) {
                guard = cand;
            }
        }

        if (!guard.valid || sameMove(guard.move, currentBest)) {
            return currentBest;
        }

        int guardGain = guard.greedyDiff - cur.greedyDiff;
        bool currentOpensBigSteal = (cur.oppGain >= 5 && cur.oppStolen >= 1);
        bool currentHasRealReply =
            cur.oppGain >= 4 ||
            cur.oppStolen >= 1;
        bool guardReducesReply =
            guard.oppGain < cur.oppGain ||
            guard.oppStolen < cur.oppStolen;
        bool modestImmediateCost = (guard.afterDiff >= cur.afterDiff - 1);
        bool respectsSearch =
            guard.move.searchScore + 350000LL >= cur.move.searchScore;
        bool directRecaptureUpgrade =
            guard.move.stolen >= 1 &&
            guard.move.gain >= 4 &&
            guard.afterDiff >= cur.afterDiff + 2 &&
            guardGain >= 2;

        if (directRecaptureUpgrade && modestImmediateCost && respectsSearch) {
            return guard.move;
        }

        if (currentHasRealReply &&
            guardReducesReply &&
            modestImmediateCost &&
            respectsSearch &&
            (guardGain >= 2 || (currentOpensBigSteal && guardGain >= 1))) {
            return guard.move;
        }

        return currentBest;
    }

    Move chooseMove(int timeLeft) {
        auto startT = chrono::steady_clock::now();

        // 이번 수 예산: 남은 시간을 적극적으로 사용하되, 저시간 구간에서는 초과 사용을 피한다.
        long long safeTime = max(1, timeLeft);
        long long reserve = safeTime >= 1000 ? 200 : 40;
        long long budget = safeTime / 8;
        budget = min<long long>(budget, max(1LL, safeTime - reserve));
        budget = max<long long>(budget, safeTime >= 300 ? 50 : 5);
        budget = min<long long>(budget, max(1LL, safeTime - 10));
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

        // iterative deepening: deadline 전까지 depth를 올림
        for (int depth = 1; depth <= 14; depth++) {
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

            // 다음 depth에서 PV를 맨 앞으로 → alpha-beta 컷 증가
            auto it = find_if(rootMoves.begin(), rootMoves.end(), [&](const Move& x) {
                return x.r1 == best.r1 && x.c1 == best.c1 &&
                       x.r2 == best.r2 && x.c2 == best.c2;
            });
            if (it != rootMoves.end()) rotate(rootMoves.begin(), it, it + 1);

            if (chrono::steady_clock::now() >= deadline) break;
        }

        best = applyGreedyResponseGuard(rootMoves, best);
        return best;
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

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
