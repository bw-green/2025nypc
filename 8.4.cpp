#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <limits>
#include <cmath>
#include <random>
#include <memory>
#include <chrono>
#include <cstring>
#include <cassert> // for sum check assertion
using namespace std;

static const int ROWS = 10;
static const int COLS = 17;
static mt19937_64 rng(chrono::steady_clock::now().time_since_epoch().count());

// Rectangle 구조체: 위치와 내 영역 개수
struct Rect {
    int r1, c1, r2, c2;
    int area;
};

class Game {
public:
    int board[ROWS][COLS];
    int myArea[ROWS][COLS];

    Game() {
        for(int i = 0; i < ROWS; ++i)
            for(int j = 0; j < COLS; ++j) {
                board[i][j] = 0;
                myArea[i][j] = 1;
            }
    }
    Game(const Game& g) {
        memcpy(board, g.board, sizeof(board));
        memcpy(myArea, g.myArea, sizeof(myArea));
    }

    void init(const vector<string>& lines) {
        for(int r = 0; r < ROWS; ++r)
            for(int c = 0; c < COLS; ++c)
                board[r][c] = lines[r][c] - '0';
    }

    vector<Rect> calculateAllMoves() {
        vector<Rect> moves;
        moves.reserve(500);
        for(int r1 = 0; r1 < ROWS; ++r1) {
            for(int r2 = r1; r2 < ROWS; ++r2) {
                for(int c1 = 0; c1 < COLS; ++c1) {
                    for(int c2 = c1; c2 < COLS; ++c2) {
                        int sumVal = 0;
                        int areaSum = 0;
                        // board가 0이 아니면 1로 카운트
                        for(int r = r1; r <= r2; ++r) {
                            for(int c = c1; c <= c2; ++c) {
                                sumVal += board[r][c];
                                areaSum += myArea[r][c];
                            }
                        }
                        if(sumVal != 10) continue;
                        bool edgeOK = true;
                        for(int r = r1; r <= r2 && edgeOK; ++r) {
                            if(board[r][c1] == 0 || board[r][c2] == 0)
                                edgeOK = false;
                        }
                        for(int c = c1; c <= c2 && edgeOK; ++c) {
                            if(board[r1][c] == 0 || board[r2][c] == 0)
                                edgeOK = false;
                        }
                        if(!edgeOK) continue;
                        moves.push_back({r1, c1, r2, c2, areaSum});
                    }
                }
            }
        }
        return moves;
    }
    vector<Rect> calculateAllMovesOpp() {
        return calculateAllMoves();
    }

    inline void applyMyMove(const Rect& mv) {
        for(int r = mv.r1; r <= mv.r2; ++r)
            for(int c = mv.c1; c <= mv.c2; ++c) {
                board[r][c] = 0;
                myArea[r][c] = 0;
            }
    }

    inline void updateOpponentAction(const Rect& mv) {
        for(int r = mv.r1; r <= mv.r2; ++r)
            for(int c = mv.c1; c <= mv.c2; ++c) {
                board[r][c] = 0;
                myArea[r][c] = 2;
            }
    }

    int getAreaScore(bool mine) const {
        int cnt = 0;
        for(int r = 0; r < ROWS; ++r)
            for(int c = 0; c < COLS; ++c)
                cnt += (mine ? (myArea[r][c] == 0) : (myArea[r][c] == 2));
        return cnt;
    }
};

struct Node {
    vector<unique_ptr<Node>> children;
    Node* parent;
    Rect move;
    double wins;
    int visits;
    vector<Rect> untried;
    Game state;
    Node(const Game& s, Node* p = nullptr, const Rect& mv = {-1,-1,-1,-1,0})
      : parent(p), move(mv), wins(0), visits(0), state(s) {
        untried = state.calculateAllMoves();
        children.reserve(8);
    }
};

double ucb1(const Node* n, double c = 1.414) {
    if(n->visits == 0) return numeric_limits<double>::infinity();
    double winRate = n->wins / n->visits;
    double pvis = n->parent ? n->parent->visits : n->visits;
    return winRate + c * sqrt(log(pvis) / n->visits);
}

double simulate(Game root) {
    bool myTurn = true;
    while(true) {
        auto moves = myTurn ? root.calculateAllMoves() : root.calculateAllMovesOpp();
        if(moves.empty()) break;
        uniform_int_distribution<size_t> dist(0, moves.size() - 1);
        Rect mv = moves[dist(rng)];
        if(myTurn) root.applyMyMove(mv);
        else        root.updateOpponentAction(mv);
        myTurn = !myTurn;
    }
    return root.getAreaScore(true) - root.getAreaScore(false);
}

Rect MCTS(const Game& rootGame, int iter = 10) {
    auto root = make_unique<Node>(rootGame);
    for(int i = 0; i < iter; ++i) {
        Node* n = root.get();
        while(n->untried.empty() && !n->children.empty()) {
            n = max_element(
                n->children.begin(), n->children.end(),
                [](auto& a, auto& b) { return ucb1(a.get()) < ucb1(b.get()); })
                ->get();
        }
        if(!n->untried.empty()) {
            Rect mv = n->untried.back();
            n->untried.pop_back();
            Game nxt = n->state;
            nxt.applyMyMove(mv);
            n->children.emplace_back(make_unique<Node>(nxt, n, mv));
            n = n->children.back().get();
        }
        double res = simulate(n->state);
        while(n) {
            n->visits++;
            n->wins += res;
            n = n->parent;
        }
    }
    auto best = max_element(
        root->children.begin(), root->children.end(),
        [](auto& a, auto& b) { return a->visits < b->visits; });
    return (*best)->move;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    Game game;
    string line;
    bool isFirst = false;
    while (getline(cin, line)) {
        if (line.empty()) continue;
        istringstream iss(line);
        string cmd;
        iss >> cmd;
        if (cmd == "READY") {
            string turn; iss >> turn;
            isFirst = (turn == "FIRST");
            cout << "OK\n";
            cout.flush();
        } else if (cmd == "INIT") {
            vector<string> boardLines(ROWS);
            for (int i = 0; i < ROWS; ++i) iss >> boardLines[i];
            game.init(boardLines);
        } else if (cmd == "TIME") {
            int myT, oppT; iss >> myT >> oppT;
            Rect best = MCTS(game, 10);
            cout << best.r1 << " " << best.c1 << " " << best.r2 << " " << best.c2 << "\n";
            cout.flush();
        } else if (cmd == "OPP") {
            int r1, c1, r2, c2, used; iss >> r1 >> c1 >> r2 >> c2 >> used;
            game.updateOpponentAction({r1, c1, r2, c2, 0});
        } else if (cmd == "FINISH") {
            break;
        }
    }
    return 0;
}
