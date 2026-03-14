// gemini 2.5 pro로 리팩토링함

#include <bits/stdc++.h>
using namespace std;

constexpr int R = 10, C = 17;

struct Move {
  int r1, c1, r2, c2;
};
inline bool isPass(const Move& m) { return m.r1 == -1; }

struct Board {
  int a[R][C]{};

  void fromStrings(const vector<string>& s) {
    for (int i = 0; i < R; i++)
      for (int j = 0; j < C; j++) a[i][j] = s[i][j] - '0';
  }

  array<array<int, C + 1>, R + 1> buildPS() const {
    array<array<int, C + 1>, R + 1> ps{};
    for (int i = 1; i <= R; i++)
      for (int j = 1; j <= C; j++) {
        int v = a[i - 1][j - 1];
        if (v < 0) v = 0;
        ps[i][j] = v + ps[i - 1][j] + ps[i][j - 1] - ps[i - 1][j - 1];
      }
    return ps;
  }

  vector<Move> genMoves() const {
    vector<Move> ms;
    auto ps = buildPS();
    for (int r1 = 0; r1 < R; ++r1) {
      for (int r2 = r1; r2 < R; ++r2) {
        vector<int> col_sum(C + 1, 0);
        for (int c = 1; c <= C; ++c) {
          int block_sum =
              ps[r2 + 1][c] - ps[r1][c] - ps[r2 + 1][c - 1] + ps[r1][c - 1];
          col_sum[c] = col_sum[c - 1] + block_sum;
        }
        for (int c1 = 0; c1 < C; ++c1) {
          int target_sum = col_sum[c1] + 10;
          auto range =
              equal_range(col_sum.begin() + c1 + 1, col_sum.end(), target_sum);
          for (auto it = range.first; it != range.second; ++it) {
            int c2 = distance(col_sum.begin(), it) - 1;
            bool t = 0, d = 0, l = 0, r = 0;
            for (int c = c1; c <= c2; c++) {
              if (a[r1][c] > 0) t = 1;
              if (a[r2][c] > 0) d = 1;
            }
            if (!t || !d) continue;
            for (int r0 = r1; r0 <= r2; r0++) {
              if (a[r0][c1] > 0) l = 1;
              if (a[r0][c2] > 0) r = 1;
            }
            if (l && r) ms.push_back({r1, c1, r2, c2});
          }
        }
      }
    }
    ms.push_back({-1, -1, -1, -1});
    return ms;
  }

  void apply(const Move& m, int pid) {
    if (isPass(m)) return;
    int val = -(pid + 1);
    for (int r = m.r1; r <= m.r2; r++)
      for (int c = m.c1; c <= m.c2; c++) a[r][c] = val;
  }

  int cntCell(int pid) const {
    int val = -(pid + 1), s = 0;
    for (int i = 0; i < R; ++i)
      for (int j = 0; j < C; ++j)
        if (a[i][j] == val) ++s;
    return s;
  }

  int gain(const Move& m, int pid) const {
    if (isPass(m)) return 0;
    int val = -(pid + 1), g = 0;
    for (int r = m.r1; r <= m.r2; r++)
      for (int c = m.c1; c <= m.c2; c++)
        if (a[r][c] != val) ++g;
    return g;
  }

  int stealCnt(const Move& m, int pid) const {
    if (isPass(m)) return 0;
    int oppVal = -((1 - pid) + 1), cnt = 0;
    for (int r = m.r1; r <= m.r2; r++)
      for (int c = m.c1; c <= m.c2; c++)
        if (a[r][c] == oppVal) ++cnt;
    return cnt;
  }
};

struct Engine {
  int myPid;
  chrono::high_resolution_clock::time_point deadline;
  // --- 개선 1: 빔 탐색 너비 증가 ---
  // 더 많은 유망한 수를 고려하여 안정성을 높입니다.
  static constexpr int BEAM = 20;

  int staticEval(const Board& b, int plyPid) const {
    int T = b.cntCell(plyPid) - b.cntCell(1 - plyPid);
    int M = b.genMoves().size() - 1;
    return (12 * T) + (1 * M);
  }

  pair<int, Move> alphabeta(Board& b, int pid, bool lastPass, int depth,
                            int alpha, int beta) {
    if (chrono::high_resolution_clock::now() >= deadline) {
      throw runtime_error("timeout");
    }

    if (lastPass && b.genMoves().size() == 1) {
      return {staticEval(b, pid), {-1, -1, -1, -1}};
    }

    if (depth == 0) {
      int stand_pat = staticEval(b, pid);
      if (stand_pat >= beta) return {beta, {-1, -1, -1, -1}};
      if (stand_pat > alpha) alpha = stand_pat;

      vector<Move> stealing_moves;
      for (const auto& m : b.genMoves()) {
        if (!isPass(m) && b.stealCnt(m, pid) > 0) {
          stealing_moves.push_back(m);
        }
      }

      if (stealing_moves.empty()) {
        return {alpha, {-1, -1, -1, -1}};
      }

      sort(stealing_moves.begin(), stealing_moves.end(),
           [&](const Move& m1, const Move& m2) {
             return b.stealCnt(m1, pid) > b.stealCnt(m2, pid);
           });

      for (const auto& m : stealing_moves) {
        Board nb = b;
        nb.apply(m, pid);
        auto [val, _] = alphabeta(nb, 1 - pid, false, 0, -beta, -alpha);
        val = -val;
        if (val >= beta) return {beta, {-1, -1, -1, -1}};
        if (val > alpha) alpha = val;
      }
      return {alpha, {-1, -1, -1, -1}};
    }

    vector<Move> moves = b.genMoves();
    Move passMove = moves.back();
    moves.pop_back();

    // --- 개선 2: 이동 순서 정렬 강화 ---
    // 1. 뺏는 칸 수(steal) > 2. 얻는 칸 수(gain) > 3. 영역 크기(area, 작을수록
    // 좋음) 효율적인 수를 먼저 탐색하여 알파-베타 가지치기 효율을 극대화합니다.
    sort(moves.begin(), moves.end(), [&](const Move& m1, const Move& m2) {
      int steal1 = b.stealCnt(m1, pid);
      int steal2 = b.stealCnt(m2, pid);
      if (steal1 != steal2) return steal1 > steal2;

      int gain1 = b.gain(m1, pid);
      int gain2 = b.gain(m2, pid);
      if (gain1 != gain2) return gain1 > gain2;

      int area1 = (m1.r2 - m1.r1 + 1) * (m1.c2 - m1.c1 + 1);
      int area2 = (m2.r2 - m2.r1 + 1) * (m2.c2 - m2.c1 + 1);
      return area1 < area2;
    });

    if (moves.size() >= BEAM) moves.resize(BEAM - 1);
    moves.push_back(passMove);

    Move bestMove = {-1, -1, -1, -1};
    for (const Move& m : moves) {
      Board nb = b;
      nb.apply(m, pid);
      auto [val, _] =
          alphabeta(nb, 1 - pid, isPass(m), depth - 1, -beta, -alpha);
      val = -val;
      if (val > alpha) {
        alpha = val;
        bestMove = m;
      }
      if (alpha >= beta) break;
    }
    return {alpha, bestMove};
  }

  Move search(Board& root, int myTimeMs, bool oppPassed) {
    // --- 개선 3: 적극적인 시간 관리 ---
    // 더 많은 시간을 탐색에 사용하여 더 깊은 수읽기를 통해 결정의 질을
    // 높입니다. 최소 200ms, 남은 시간의 약 6.7%(1/15)를 사용합니다.
    int searchTimeMs = max(200, myTimeMs / 15);
    deadline = chrono::high_resolution_clock::now() +
               chrono::milliseconds(searchTimeMs);
    Move bestMove = {-1, -1, -1, -1};
    for (int depth = 1;; ++depth) {
      try {
        auto [score, move] =
            alphabeta(root, myPid, oppPassed, depth, -1e9, 1e9);
        bestMove = move;
      } catch (const runtime_error&) {
        break;
      }
    }
    return bestMove;
  }
};

int main() {
  ios::sync_with_stdio(false);
  cin.tie(nullptr);
  Board board;
  int myPid = -1;
  string cmd;
  Engine engine;
  bool opponentPassed = false;
  // --- 개선 4: 턴 카운터 추가 (확장성) ---
  int turn_count = 0;
  while (cin >> cmd) {
    if (cmd == "READY") {
      string role;
      cin >> role;
      myPid = (role == "FIRST") ? 0 : 1;
      engine.myPid = myPid;
      cout << "OK\n" << flush;
    } else if (cmd == "INIT") {
      vector<string> rows(R);
      for (auto& s : rows) cin >> s;
      board.fromStrings(rows);
    } else if (cmd == "OPP") {
      Move m;
      int t;
      cin >> m.r1 >> m.c1 >> m.r2 >> m.c2 >> t;
      opponentPassed = isPass(m);
      if (!opponentPassed) board.apply(m, 1 - myPid);
    } else if (cmd == "TIME") {
      int myT, oppT;
      cin >> myT >> oppT;
      turn_count++;
      Move mv = engine.search(board, myT, opponentPassed);
      board.apply(mv, myPid);
      opponentPassed = false;
      cout << mv.r1 << ' ' << mv.c1 << ' ' << mv.r2 << ' ' << mv.c2 << '\n'
           << flush;
    } else if (cmd == "FINISH") {
      break;
    }
  }
  return 0;
}