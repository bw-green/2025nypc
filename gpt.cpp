#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <climits>
#include <unordered_set>
using namespace std;

static const int ROWS = 10;
static const int COLS = 17;
struct Rect {
    int r1, c1, r2, c2, area;
    bool operator==(const Rect& o) const noexcept {
        return r1==o.r1 && c1==o.c1&& r2==o.r2
            && c2==o.c2;
    }
};




class Game {
public:
    vector<vector<int>> board;
    vector<vector<int>> ownership;  // 비어 있으면 1 내꺼면 0 상대꺼면 2
    Game() : board(ROWS, vector<int>(COLS, 0)), ownership(ROWS, vector<int>(COLS, 0)) {}
    void init(const vector<string>& lines) {
        for (int r = 0; r < ROWS; ++r)
            for (int c = 0; c < COLS; ++c){
                board[r][c] = lines[r][c] - '0';
                ownership[r][c]=1;
            }
                
    }

    vector<vector<int>> buildPS(const vector<vector<int>>& b) {
        vector<vector<int>> PS(ROWS+1, vector<int>(COLS+1, 0));
        for (int r = 0; r < ROWS; ++r)
            for (int c = 0; c < COLS; ++c)
                PS[r+1][c+1] = PS[r][c+1] + PS[r+1][c] - PS[r][c] + b[r][c];
        return PS;
    }
    vector<vector<int>> buildOpnentArea(const vector<vector<int>>& b) {
        vector<vector<int>> Area(ROWS+1, vector<int>(COLS+1, 0));
        for (int r = 0; r < ROWS; ++r)
            for (int c = 0; c < COLS; ++c){
                int temp = b[r][c];
                if(temp ==2) temp=0;
                else if(temp==0) temp=2;
                Area[r+1][c+1] = Area[r][c+1] + Area[r+1][c] - Area[r][c] + temp;
            }
                
        return Area;
    }
    int getSum(const vector<vector<int>>& PS, int r1, int c1, int r2, int c2) {
        return PS[r2+1][c2+1] - PS[r1][c2+1] - PS[r2+1][c1] + PS[r1][c1];
    }
    vector<int> calculateMove() {
        // Generate all valid rectangles initial
        vector<Rect> candidates;
        auto PS0 = buildPS(board);
        auto Area0 = buildPS(ownership);
        bool banned[ROWS][COLS][ROWS][COLS] = {}; 
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
        // Choose best by full greedy simulation
        int bestScore = INT_MIN;
        Rect bestRect = {-1,-1,-1,-1,0};
        sort(candidates.begin(), candidates.end(),
        [](const Rect& a, const Rect& b) {
            return a.area > b.area;
        }
        );
        // if(candidates.size()>0){
        //     bestRect.c1=candidates[0].c1;
        //     bestRect.r1=candidates[0].r1;
        //     bestRect.r2=candidates[0].r2;
        //     bestRect.c2=candidates[0].c2;
        // }
        for (auto &c : candidates) {
            // temp board
            vector<vector<int>> btemp = board;
            vector<vector<int>> atemp = ownership;
            int totalGain = 0;
            // apply initial
            for (int r = c.r1; r <= c.r2; ++r)
                for (int cc = c.c1; cc <= c.c2; ++cc){
                    btemp[r][cc] = 0;atemp[r][cc] = 0;
                }
            totalGain += c.area;
            // greedy chain
            totalGain -=stimulation(btemp,atemp,totalGain,banned);
            if(totalGain>=0){
                bestRect.c1=c.c1;
                bestRect.r1=c.r1;
                bestRect.r2=c.r2;
                bestRect.c2=c.c2;
                break;
            }
            else{
                banned[c.c1][c.r1][c.r2][c.c2]=true;
            }
        }
        vector<int> res = {bestRect.r1, bestRect.c1, bestRect.r2, bestRect.c2};
        if (res[0] != -1) {
            applyMyMove(res[0],res[1],res[2],res[3]);
            
        }
        return res;
    }

    int stimulation(vector<vector<int>> &sboard,vector<vector<int>> &sarea, int lastarea,bool ban[ROWS][COLS][ROWS][COLS]){
        // 내턴이면 나보다 많은 영역 차지 할수 있다 -> 더 들어가서 내가 더 먹을 수 있는지 판단
        // 나보다 적으면 패스
        
        vector<Rect> candidates;
        auto PS = buildPS(sboard);
        auto Area= buildOpnentArea(sarea);

        for (int r1 = 0; r1 < ROWS; ++r1) for (int r2 = r1; r2 < ROWS; ++r2)
            for (int c1 = 0; c1 < COLS; ++c1) for (int c2 = c1; c2 < COLS; ++c2) {
                if (getSum(PS, r1, c1, r2, c2) != 10) continue;
                if (ban[r1][c1][r2][c2]) continue;
                bool ok = true;
                auto checkEdge = [&](int rr, int cc, int dr, int dc) {
                    if(getSum(PS,rr,cc,dr,cc)==0) return false;
                    if(getSum(PS,rr,dc,dr,dc)==0)return false;
                    if(getSum(PS,rr,cc,rr,dc)==0)return false;
                    if(getSum(PS,dr,cc,dr,dc)==0)return false;
                    return true;
                };

                if (!checkEdge(r1, c1, r2, c2)) continue;
                int curarea=getSum(Area,r1, c1, r2, c2);
                if(curarea>lastarea){
                    candidates.push_back({r1, c1, r2, c2, curarea});
                }
        }
        // Choose best by full greedy simulation
        int bestScore = INT_MIN;
        Rect bestRect = {-1,-1,-1,-1,0};
        sort(candidates.begin(), candidates.end(),
        [](const Rect& a, const Rect& b) {
            return a.area > b.area;
        }
        );
        if(candidates.size()==0) return 0;

        for (auto &c : candidates) {
            // temp board
            vector<vector<int>> btemp = sboard;
            vector<vector<int>> atemp = sarea;
            int totalGain = 0;
            // apply initial
            for (int r = c.r1; r <= c.r2; ++r)
                for (int cc = c.c1; cc <= c.c2; ++cc){
                    btemp[r][cc] = 0;atemp[r][cc] = 0;
                }
            totalGain += c.area;
            // // greedy chain
            // // totalGain -= stimulation(btemp,atemp,totalGain);
            if(totalGain>lastarea){// 딴놈이 더큼 
                return totalGain;
            }
            // else{// 이거 하면 손해임
            //     ban[c.r1][c.c1][c.r2][c.c2]=true;
            // }
        }
        return 0;
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
                ownership[r][c] = 0;
    }
    void updateYoursArea(int r1, int c1, int r2, int c2){
        if (r1 == -1) return;
        for (int r = r1; r <= r2; ++r)
            for (int c = c1; c <= c2; ++c)
                ownership[r][c] = 2;
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
