#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <cstring>

using namespace std;




// 게임 상태를 관리하는 클래스
class Game
{
private:
    vector<vector<int>> board; // 게임 보드 (2차원 벡터)
    bool first;                // 선공 여부
    bool passed;               // 마지막 턴에 패스했는지 여부
    int bit[11][18];           // 1 based
    int n=10,m=17;
    int square[11][18];
public:
    Game() {}

    Game(const vector<vector<int>> &board, bool first)
        : board(board), first(first), passed(false) {
            memset(bit, 0, sizeof(bit));
            for(int i=1; i<=n; i++){
                for(int j=1; j<=m; j++){
                    update(i,j,board[i-1][j-1]);
                }
            }
        }

    int getPrefixSum(int r, int c) {
        int sum = 0;
        while (r > 0) {
            int cc = c;
            while (cc > 0) {
                sum += bit[r][cc];
                cc -= cc&-cc;
            }
            r -= r&-r;
        }
        return sum;
    }

    void update(int r, int c, int diff) {
        while (r <= n) {
            int cc = c;
            while (cc <= m) {
                bit[r][cc] += diff;
                cc += cc&-cc;
            }
            r += r&-r;
        }
    }

    int query(int x1, int y1, int x2, int y2) {
        return getPrefixSum(x2, y2) - getPrefixSum(x2, y1-1) - getPrefixSum(x1-1,y2) + getPrefixSum(x1-1, y1-1);
    }
    // 사각형 (r1, c1) ~ (r2, c2)이 유효한지 검사 (합이 10이고, 네 변을 모두 포함)
    bool isValid(int r1, int c1, int r2, int c2)
    {
        int sums = 0;
        bool r1fit = false, c1fit = false, r2fit = false, c2fit = false;

        for (int r = r1; r <= r2; r++)
            for (int c = c1; c <= c2; c++)
                if (board[r][c] != 0)
                {
                    sums += board[r][c];
                    if (r == r1)
                        r1fit = true;
                    if (r == r2)
                        r2fit = true;
                    if (c == c1)
                        c1fit = true;
                    if (c == c2)
                        c2fit = true;
                }
        return (sums == 10) && r1fit && r2fit && c1fit && c2fit;
        // bool can1 =false,can2=false,can3=false,can4=false;
        // for(int i=r1; i<r2; i++){
        //     if(board[i][c1]!=0){
        //         can1=true;
        //         break;
        //     }
        // }
        // for(int i=r1; i<r2; i++){
        //     if(board[i][c2]!=0){
        //         can2=true;
        //         break;
        //     }
        // }
        // for(int i=c1; i<c2; i++){
        //     if(board[r1][i]!=0){
        //         can3=true;
        //         break;
        //     }
        // }
        // for(int i=c1; i<c2; i++){
        //     if(board[r2][i]!=0){
        //         can4=true;
        //         break;
        //     }
        // }
        // return can1&&can2&&can3&&can4;
    }

    int calarea(int r1, int c1, int r2, int c2){  // 내가 먹은거 상대가 먹은거 해서 정확히 얼마 먹는지 계산해야함
        return (r2-r1+1)*(c2-c1+1);
    }

    // ================================================================
    // ===================== [필수 구현] ===============================
    // 합이 10인 유효한 사각형을 찾아 {r1, c1, r2, c2} 벡터로 반환
    // 없으면 {-1, -1, -1, -1}을 반환하여 패스를 의미함
    // ================================================================
   
    vector<int> calculateMove(int myTime, int oppTime)
    {
        int area=0;   // 실제로 자기가 차지 하는 영역의 넓이 나중에 계산기 만들어야함 
        int a1,a2,a3,a4;
        
        for (int r1 = 0; r1 < board.size(); r1++)
            for (int c1 = 0; c1 < board[r1].size() - 1; c1++)
            {
                int r2 = r1;
                int c2 = c1 + 1;
                if (isValid(r1, c1, r2, c2))
                    return {r1, c1, r2, c2};
            }
        for (int r1 = 0; r1 < board.size()-1; r1++)
            for (int c1 = 0; c1 < board[r1].size(); c1++)
            {
                int r2 = r1+1;
                int c2 = c1;
                if (isValid(r1, c1, r2, c2))
                    return {r1, c1, r2, c2};
            }
        // for(int l=0; l<10; l++){
        //     for(int h=0; h<10; h++){  // 여기까지는 왼쪽 위 시작 지점 노가다 전

        //         for(int r=l; r<l+3; r++){    // 오른쪽 끝을 정함 
        //             int temp = query(l+1,h+1,r+1,h+1);
        //             if(temp>10) break;
        //             for(int p=h; p<h+3; p++){
        //                 int temp = query(l+1,h+1,r+1,p+1);
        //                 if(temp>10) break;
        //                 if(!isValid(l,h,r,p)) continue;   // 애초에 불가능 한거면 패스
        //                 if(temp==10){
        //                     int temparea = calarea(l,h,r,p);
        //                     if(temparea>area){
        //                         area=temparea;
        //                         a1=l,a2=h,a3=r,a4=p;
        //                     }
        //                     return {a1,a2,a3,a4};
        //                 }
        //             }
                    
        //         }
        //     }
        // }
        // if(area==0) return {-1, -1, -1, -1};
        // else{
        //     return {a1,a2,a3,a4};
        // }


        // // 가로로 인접한 두 칸을 선택했을 때 유효하면 선택하는 전략
        // for (int r1 = 0; r1 < board.size(); r1++)
        //     for (int c1 = 0; c1 < board[r1].size() - 1; c1++)
        //     {
        //         int r2 = r1;
        //         int c2 = c1 + 1;
        //         if (isValid(r1, c1, r2, c2))
        //             return {r1, c1, r2, c2};
        //     }
        return {-1, -1, -1, -1}; // 유효한 사각형이 없으면 패스
    }
    // =================== [필수 구현 끝] =============================

    // 상대방의 수를 받아 보드에 반영
    void updateOpponentAction(const vector<int> &action, int time)
    {
        updateMove(action[0], action[1], action[2], action[3], false);
    }

    // 주어진 수를 보드에 반영 (칸을 0으로 지움)
    void updateMove(int r1, int c1, int r2, int c2, bool isMyMove)
    {
        if (r1 == -1 && c1 == -1 && r2 == -1 && c2 == -1)
        {
            passed = true;
            return;
        }
        for (int r = r1; r <= r2; r++)
            for (int c = c1; c <= c2; c++){
                update(r+1,c+1,-board[r][c]);
                board[r][c] = 0;
            }
                
        passed = false;
    }
};

// 표준 입력을 통해 명령어를 처리하는 메인 함수
int main()
{
    Game game;
    bool first = false;

    while (true)
    {
        string line;
        getline(cin, line);

        istringstream iss(line);
        string command;
        if (!(iss >> command))
            continue;

        if (command == "READY")
        {
            // 선공 여부 확인
            string turn;
            iss >> turn;
            first = (turn == "FIRST");
            cout << "OK" << endl;
            continue;
        }

        if (command == "INIT")
        {
            // 보드 초기화
            vector<vector<int>> board;
            string row;
            while (iss >> row)
            {
                vector<int> boardRow;
                for(int j=0; j<row.size(); j++){
                    boardRow.push_back(row[j] - '0');
                }
                board.push_back(boardRow);
            }
            
            game = Game(board, first);
            continue;
        }

        if (command == "TIME")
        {
            // 내 차례: 수 계산 및 출력
            int myTime, oppTime;
            iss >> myTime >> oppTime;

            vector<int> ret = game.calculateMove(myTime, oppTime);
            game.updateMove(ret[0], ret[1], ret[2], ret[3], true);
            cout<<myTime<<" "<<oppTime<<"\n";

            cout << ret[0] << " " << ret[1] << " " << ret[2] << " " << ret[3] << endl; // 내 행동 출력
            continue;
        }

        if (command == "OPP")
        {
            // 상대 행동 반영
            int r1, c1, r2, c2, time;
            iss >> r1 >> c1 >> r2 >> c2 >> time;
            game.updateOpponentAction({r1, c1, r2, c2}, time);
            continue;
        }

        if (command == "FINISH")
        {
            // 게임 종료
            break;
        }

        // 알 수 없는 명령 처리
        cerr << "Invalid command: " << command << endl;
        return 1;
    }

    return 0;
}