#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <climits>
using namespace std;

static const int ROWS = 10;
static const int COLS = 17;
struct Rect { int r1,c1,r2,c2, area; };
class Game {
public:
    vector<vector<int>> board;
    vector<vector<int>> myArea;  // 비어 있으면 1 내꺼면 0 상대꺼면 2
    vector<vector<int>> yourArea; // 상대 기준의 영역 
    vector<vector<int>> PS;
    vector<vector<int>> AreaMinePS,AreayoursPS;
    Game() : board(ROWS, vector<int>(COLS, 0)), myArea(ROWS, vector<int>(COLS, 1)), yourArea(ROWS, vector<int>(COLS, 1)) {}
    void init(const vector<string>& lines) {
        for (int r = 0; r < ROWS; ++r)
            for (int c = 0; c < COLS; ++c){
                board[r][c] = lines[r][c] - '0';
            }
        PS = buildPS(board);
        AreaMinePS = buildArea(myArea);
        AreayoursPS = buildArea(yourArea);
    }

    vector<vector<int>> buildPS(const vector<vector<int>>& b) {
        vector<vector<int>> PS(ROWS+1, vector<int>(COLS+1, 0));
        for (int r = 0; r < ROWS; ++r)
            for (int c = 0; c < COLS; ++c)
                PS[r+1][c+1] = PS[r][c+1] + PS[r+1][c] - PS[r][c] + b[r][c];
        return PS;
    }
    vector<vector<int>> buildArea(const vector<vector<int>>& b) {
        vector<vector<int>> Area(ROWS+1, vector<int>(COLS+1, 0));
        for (int r = 0; r < ROWS; ++r)
            for (int c = 0; c < COLS; ++c)
                Area[r+1][c+1] = Area[r][c+1] + Area[r+1][c] - Area[r][c] + b[r][c];
        return Area;
    }
    int getSum(const vector<vector<int>>& PS, int r1, int c1, int r2, int c2) {
        return PS[r2+1][c2+1] - PS[r1][c2+1] - PS[r2+1][c1] + PS[r1][c1];
    }
    vector<Rect> calculateAllMoves(vector<vector<int>>& nowBoard, vector<vector<int>>& nowArea) {
        vector<Rect> candidates;
        auto PS0 = buildPS(nowBoard);
        auto Area0 = buildArea(nowArea);
        for (int r1 = 0; r1 < ROWS; ++r1) for (int r2 = r1; r2 < ROWS; ++r2)
            for (int c1 = 0; c1 < COLS; ++c1) for (int c2 = c1; c2 < COLS; ++c2) {
                if (getSum(PS0, r1, c1, r2, c2) != 10) continue;
                bool ok = true;
                auto checkEdge = [&](int rr, int cc, int dr, int dc) {
                    int flag=1;
                    if(getSum(PS0,rr,cc,dr,cc)==0) return false;
                    if(getSum(PS0,rr,dc,dr,dc)==0)return false;
                    if(getSum(PS0,rr,cc,rr,dc)==0)return false;
                    if(getSum(PS0,dr,cc,dr,dc)==0)return false;
                    return true;
                };
                int width = c2 - c1 + 1;
                int height = r2 - r1 + 1;
                if (!checkEdge(r1, c1, r2, c2)) continue;
                candidates.push_back({r1, c1, r2, c2, getSum(Area0,r1, c1, r2, c2)});
            }
        sort(candidates.begin(), candidates.end(),
            [](const Rect& a, const Rect& b) { return a.area > b.area; });
        return candidates;
    }
    Rect getBestMove(bool myTurn, int iter,
                 vector<vector<int>>& nowBoard,
                 vector<vector<int>>& nowArea) {
    if(iter <= 0) return {-1,-1,-1,-1,0};
    auto candidates = calculateAllMoves(nowBoard, nowArea);
    for(auto &mv : candidates) {
        // copy state
        auto bCopy = nowBoard;
        auto aCopy = nowArea;
        // apply move
        if(myTurn) {
            for(int r = mv.r1; r <= mv.r2; ++r)
                for(int c = mv.c1; c <= mv.c2; ++c) {
                    bCopy[r][c] = 0;
                    aCopy[r][c] = 0;
                }
        } else {
            for(int r = mv.r1; r <= mv.r2; ++r)
                for(int c = mv.c1; c <= mv.c2; ++c) {
                    bCopy[r][c] = 0;
                    aCopy[r][c] = 2;
                }
        }
        // opponent best response
        Rect opp = getBestMove(!myTurn, iter - 1, bCopy, aCopy);
        if(opp.area <= mv.area) {
            return mv;
        }
        // else continue to next candidate
    }
    return {-1,-1,-1,-1,0};
}
    Rect calculateMove() {
        // Generate all valid rectangles initial
        
    
        return getBestMove(true, 10, board, myArea);
    }
    void updateOpponentAction(int r1, int c1, int r2, int c2){
        if (r1 == -1) return;
        updateYoursArea(r1,c1,r2, c2);
        for (int r = r1; r <= r2; ++r)
            for (int c = c1; c <= c2; ++c)
                board[r][c] = 0;
    }
    void applyMyMove(int r1, int c1, int r2, int c2) {
        if (r1 == -1) return;
        updateMyArea(r1,c1,r2, c2);
        for (int r = r1; r <= r2; ++r)
            for (int c = c1; c <= c2; ++c)
                board[r][c] = 0;
    }
    void updateMyArea(int r1, int c1, int r2, int c2){
        if (r1 == -1) return;
        for (int r = r1; r <= r2; ++r)
            for (int c = c1; c <= c2; ++c)
                myArea[r][c] = 0;
    }
    void updateYoursArea(int r1, int c1, int r2, int c2){
        if (r1 == -1) return;
        for (int r = r1; r <= r2; ++r)
            for (int c = c1; c <= c2; ++c)
                myArea[r][c] = 2;
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    Game game;
    bool isFirst = false;
    string line;
    while (getline(cin, line)) {
        if (line.empty()) continue;
        istringstream iss(line);
        string cmd;
        iss >> cmd;
        if (cmd == "READY") {
            string turn;
            iss >> turn;
            isFirst = (turn == "FIRST");
            cout << "OK\n";
            cout.flush();
        } else if (cmd == "INIT") {
            vector<string> boardLines(ROWS);
            for (int i = 0; i < ROWS; ++i) iss >> boardLines[i];
            game.init(boardLines);
        } else if (cmd == "TIME") {
            int myT, oppT;
            iss >> myT >> oppT;
            auto best = game.calculateMove();
            cout << best.r1 << " " << best.c1 << " " << best.r2 << " " << best.c2 << "\n";
            cout.flush();
        } else if (cmd == "OPP") {
            int r1, c1, r2, c2, used;
            iss >> r1 >> c1 >> r2 >> c2 >> used;
            game.updateOpponentAction(r1, c1, r2, c2);
        } else if (cmd == "FINISH") {
            break;
        }
    }
    return 0;
}
