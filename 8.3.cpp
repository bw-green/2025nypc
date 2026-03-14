#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <climits>
#include <queue>
using namespace std;

static const int ROWS = 10;
static const int COLS = 17;
struct Rect { int r1,c1,r2,c2, area; };
struct RectCmp {
    bool operator()(const Rect& a, const Rect& b) const {
        return a.area < b.area;
    }
};
class Game { 
public:
    vector<vector<int>> board;
    vector<vector<int>> myArea;  // 비어 있으면 1 내꺼면 0 상대꺼면 2
    vector<vector<int>> yourArea; // 상대 기준의 영역 
    vector<vector<int>> PS;
    vector<vector<int>> AreaMinePS,AreayoursPS;
    vector<Rect> candidates;
    priority_queue<Rect, std::vector<Rect>, RectCmp> pq;
    Game() : board(ROWS, vector<int>(COLS, 0)), myArea(ROWS, vector<int>(COLS, 1)), yourArea(ROWS, vector<int>(COLS, 1)) {}
    void init(const vector<string>& lines) {
        for (int r = 0; r < ROWS; ++r)
            for (int c = 0; c < COLS; ++c){
                board[r][c] = lines[r][c] - '0';
            }
        PS = buildPS(board);
        AreaMinePS = buildPS(myArea);
        AreayoursPS = buildPS(yourArea);
    
        for (int r1 = 0; r1 < ROWS; ++r1) for (int r2 = r1; r2 < ROWS; ++r2)
            for (int c1 = 0; c1 < COLS; ++c1) for (int c2 = c1; c2 < COLS; ++c2) {
                if (getSum(PS, {r1, c1, r2, c2,0}) != 10) continue;
                bool ok = true;
                if (!edgeCheck(r1, c1, r2, c2)) continue;
                candidates.push_back({r1, c1, r2, c2, getSum(AreaMinePS,{r1, c1, r2, c2,0})});
            }
        pq = priority_queue<Rect, vector<Rect>, RectCmp>(RectCmp(), candidates);
    }

    vector<vector<int>> buildPS(const vector<vector<int>>& b) {
        vector<vector<int>> PS(ROWS+1, vector<int>(COLS+1, 0));
        for (int r = 0; r < ROWS; ++r)
            for (int c = 0; c < COLS; ++c)
                PS[r+1][c+1] = PS[r][c+1] + PS[r+1][c] - PS[r][c] + b[r][c];
        return PS;
    }
    
    int getSum(const vector<vector<int>>& PS, Rect r) {
        return PS[r.r2+1][r.c2+1] - PS[r.r1][r.c2+1] - PS[r.r2+1][r.c1] + PS[r.r1][r.c1];
    }
    vector<int> calculateMove() {
        int bestScore = INT_MIN;
        Rect bestRect = {-1,-1,-1,-1,0};
        while (!pq.empty() 
            && ( getSum(PS,pq.top()) < 10
                || !edgeCheck(pq.top().r1,pq.top().c1,
                                pq.top().r2,pq.top().c2) )
            )
        {
            pq.pop();
        }
        // 2) 남은 게 없으면 패스
        if (pq.empty()) {
            return { -1, -1, -1, -1 };
        }
        // 3) 최상위(=가장 넓은 합=10 후보) 꺼내서 적용
        Rect best = pq.top(); 
        pq.pop();
        applyMyMove(best.r1, best.c1, best.r2, best.c2);
        return { best.r1, best.c1, best.r2, best.c2 };
    }
    void recomputeCandidatesInUpdatedRegion(int ur1, int uc1, int ur2, int uc2) {

        for (int r1 = 0; r1 < ROWS; ++r1) {
            for (int r2 = r1; r2 < ROWS; ++r2) {
                if(r2<ur1) continue;    // 세로 전혀 겹치지 않으면 skip
                if(r1>ur2) break;
                for (int c1 = 0; c1 < COLS; ++c1) {
                    for (int c2 = c1; c2 < COLS; ++c2) {
                        if (c2 <uc1) continue; // 가로 전혀 겹치지 않으면 skip
                        if  ( c1 > uc2) break;
                        
                        Rect R{r1, c1, r2, c2, 0};
                        // 1) 사과 합이 10인 사각형만
                        if (getSum(PS, R) != 10) continue;
                        // 2) 테두리에 사과가 있는지
                        if (!edgeCheck(r1, c1, r2, c2)) continue;
                        // 3) 내 영역 면적 계산
                        R.area = getSum(AreaMinePS, R);
                        pq.push(R);
                    }
                }
            }
        }
    }   
    bool edgeCheck(int rr, int cc, int dr, int dc) {
        if(getSum(PS,{rr,cc,dr,cc,0})==0) return false;
        if(getSum(PS,{rr,dc,dr,dc,0})==0)return false;
        if(getSum(PS,{rr,cc,rr,dc,0})==0)return false;
        if(getSum(PS,{dr,cc,dr,dc,0})==0)return false;
        return true;
    }
    void updateOpponentAction(int r1, int c1, int r2, int c2){
        if (r1 == -1) return;
        updateYoursArea(r1,c1,r2, c2);
        for (int r = r1; r <= r2; ++r)
            for (int c = c1; c <= c2; ++c)
                board[r][c] = 0;
        updatePartialPS(PS,r1, c1, r2, c2);
        AreaMinePS=buildPS(myArea);
        AreayoursPS=buildPS(yourArea);
        recomputeCandidatesInUpdatedRegion(r1, c1, r2, c2);
    }
    void applyMyMove(int r1, int c1, int r2, int c2) {
        if (r1 == -1) return;
        updateMyArea(r1,c1,r2, c2);
        for (int r = r1; r <= r2; ++r)
            for (int c = c1; c <= c2; ++c)
                board[r][c] = 0;
        updatePartialPS(PS,r1, c1, r2, c2);
        AreaMinePS=buildPS(myArea);
        AreayoursPS=buildPS(yourArea);
        recomputeCandidatesInUpdatedRegion(r1, c1, r2, c2);
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
    void updatePartialPS(
    vector<std::vector<int>>& ps,
    int r1, int c1, int r2, int c2
    ) {
        int R = ps.size() - 1;
        int C = ps[0].size() - 1;

        int r2p1 = r2 + 1;
        int c2p1 = c2 + 1;

        // 경계 행/열 값 보관
        std::vector<int> row_r1(C+1), row_r2p1(C+1);
        std::vector<int> col_c1(R+1), col_c2p1(R+1);
        for (int j = 0; j <= C; ++j) {
            row_r1[j]   = ps[r1][j];
            row_r2p1[j] = ps[r2p1][j];
        }
        for (int i = 0; i <= R; ++i) {
            col_c1[i]   = ps[i][c1];
            col_c2p1[i] = ps[i][c2p1];
        }

        // 제거할 사각형 전체 합 계산
        int S_total = row_r2p1[c2p1] - row_r1[c2p1]
                    - row_r2p1[c1]   + row_r1[c1];

        // 부분 갱신: 역순(i↓, j↓)으로 PS 업데이트
        for (int i = R; i >= r1 + 1; --i) {
            for (int j = C; j >= c1 + 1; --j) {
                int removed;
                if (i > r2 && j > c2) {
                    // 사각형 전체 영역이 ps[i][j]에 포함
                    removed = S_total;
                } else if (i > r2) {
                    // 행 전부, 열 일부(c1..j-1)
                    removed = row_r2p1[j] - row_r1[j]
                            - row_r2p1[c1] + row_r1[c1];
                } else if (j > c2) {
                    // 열 전부, 행 일부(r1..i-1)
                    removed = col_c2p1[i] - col_c2p1[r1]
                            - col_c1[i]   + col_c1[r1];
                } else {
                    // 사각형 내부(i-1,j-1)까지
                    removed = ps[i][j]
                            - row_r1[j]
                            - col_c1[i]
                            + row_r1[c1];
                }
                ps[i][j] -= removed;
            }
        }
    }
    // void updatePS(int r1, int c1, int r2, int c2){
    //     int r2p1 = r2+1, c2p1 = c2+1;
    //     vector<int> row_r1(COLS+1), row_r2p1(COLS+1),
    //                 col_c1(ROWS+1), col_c2p1(ROWS+1);
    //     for(int j=0; j<=COLS; ++j){
    //         row_r1[j]    = PS[r1][j];
    //         row_r2p1[j]  = PS[r2p1][j];
    //     }
    //     for(int i=0; i<=ROWS; ++i){
    //         col_c1[i]    = PS[i][c1];
    //         col_c2p1[i]  = PS[i][c2p1];
    //     }
    //     // 전체 제거합 S_total
    //     int S_total = row_r2p1[c2p1] - row_r1[c2p1]
    //                 - row_r2p1[c1]   + row_r1[c1];

    //     // 2) board를 0으로 덮어쓰기
    //     for(int r=r1; r<=r2; ++r)
    //         for(int c=c1; c<=c2; ++c)
    //             board[r][c] = 0;

    //     // 3) PS를 역순(i↓, j↓)으로 한 번만 순회하며 업데이트
    //     for(int i=ROWS; i>=r1+1; --i){
    //         for(int j=COLS; j>=c1+1; --j){
    //             int removed;
    //             if (i>r2 && j>c2) {
    //                 // (r1..r2, c1..c2) 전부 빠짐
    //                 removed = S_total;
    //             }
    //             else if (i>r2) {
    //                 // 사각형 세로 전부 & 가로 일부 (c1..j-1)
    //                 removed = row_r2p1[j] - row_r1[j]
    //                         - row_r2p1[c1] + row_r1[c1];
    //             }
    //             else if (j>c2) {
    //                 // 사각형 가로 전부 & 세로 일부 (r1..i-1)
    //                 removed = col_c2p1[i] - col_c2p1[r1]
    //                         - col_c1[i]   + col_c1[r1];
    //             }
    //             else {
    //                 // 사각형 내부(i-1,j-1)까지
    //                 removed = PS[i][j]
    //                         - row_r1[j]
    //                         - col_c1[i]
    //                         + row_r1[c1];
    //             }
    //             PS[i][j] -= removed;
    //         }
    //     }
    // }
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
            auto mv = game.calculateMove();
            cout << mv[0] << ' ' << mv[1] << ' ' << mv[2] << ' ' << mv[3] << "\n";
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
