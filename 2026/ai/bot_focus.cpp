// 포커스 탐색 봇: 평가는 bot_strong 그대로, 탐색만 "직전 수에 영향받는(겹치는) 후보만"으로 좁혀
//   분기수를 줄이고 같은 시간에 더 깊이(>=5) 본다. + 안전용 글로벌 상위 K 후보 항상 포함.
//   상대가 직사각형을 점령하면 그 영역의 칸만 바뀌므로, 그 영역과 겹치는 직사각형만 합/소유가 변함.
#include <bits/stdc++.h>
using namespace std;

int FIXED_DEPTH = 0;
int SAFE_K = 6;          // 항상 포함할 글로벌 상위 후보 수(비국소 호수 방지)

struct Move {
    int r1=-1,c1=-1,r2=-1,c2=-1;
    int area=0, neutral=0, stolen=0, own=0, gain=0;
    long long orderScore=0, searchScore=0;
    bool isPass() const { return r1<0; }
};
static bool bboxOverlap(const Move&a,const Move&b){
    if(a.isPass()||b.isPass())return false;
    return !(a.r2<b.r1 || b.r2<a.r1 || a.c2<b.c1 || b.c2<a.c1);
}

struct Game {
    static constexpr int R=10, C=17;
    int val[R][C]{}; bool alive[R][C]{}; int owner[R][C]{};
    int me=1, opp=2; bool lastPass=false;
    Move lastOpp;                    // 상대의 직전 수(루트 포커스 기준)
    mutable bool timeUp=false;
    mutable chrono::steady_clock::time_point deadline;
    mutable long long nodeCnt=0;

    bool outOfTime() const {
        if(FIXED_DEPTH>0) return false;
        if(timeUp) return true;
        if((++nodeCnt & 255)==0)
            if(chrono::steady_clock::now()>=deadline) timeUp=true;
        return timeUp;
    }
    void init(const vector<string>& rows){
        memset(owner,0,sizeof(owner)); lastPass=false; lastOpp=Move{};
        for(int r=0;r<R;r++)for(int c=0;c<C;c++){val[r][c]=rows[r][c]-'0';alive[r][c]=true;}
    }
    int diffScore(int player) const {
        int e=3-player,a=0,b=0;
        for(int r=0;r<R;r++)for(int c=0;c<C;c++){if(owner[r][c]==player)a++;else if(owner[r][c]==e)b++;}
        return a-b;
    }
    int occupiedCount() const { int n=0; for(int r=0;r<R;r++)for(int c=0;c<C;c++)if(owner[r][c])n++; return n; }
    long long terminalScore(int player) const {
        int d=diffScore(player);
        if(d>0)return 1000000000LL+1000000LL*d;
        if(d==0)return 0;
        return -1000000000LL+1000000LL*d;
    }
    Move makeMove(int r1,int c1,int r2,int c2,int player) const {
        Move m; m.r1=r1;m.c1=c1;m.r2=r2;m.c2=c2; m.area=(r2-r1+1)*(c2-c1+1); int e=3-player;
        for(int r=r1;r<=r2;r++)for(int c=c1;c<=c2;c++){
            if(owner[r][c]==0)m.neutral++; else if(owner[r][c]==e)m.stolen++; else m.own++;
        }
        m.gain=m.neutral+2*m.stolen;
        m.orderScore=120000LL*m.gain+70000LL*m.stolen+9000LL*m.neutral+300LL*m.area-2000LL*m.own;
        return m;
    }
    vector<Move> getCandidates(int player) const {
        int psV[R+1][C+1]{}, psA[R+1][C+1]{};
        for(int r=0;r<R;r++)for(int c=0;c<C;c++){
            int v=alive[r][c]?val[r][c]:0, a=alive[r][c]?1:0;
            psV[r+1][c+1]=psV[r][c+1]+psV[r+1][c]-psV[r][c]+v;
            psA[r+1][c+1]=psA[r][c+1]+psA[r+1][c]-psA[r][c]+a;
        }
        auto rV=[&](int r1,int c1,int r2,int c2){return psV[r2+1][c2+1]-psV[r1][c2+1]-psV[r2+1][c1]+psV[r1][c1];};
        auto rA=[&](int r1,int c1,int r2,int c2){return psA[r2+1][c2+1]-psA[r1][c2+1]-psA[r2+1][c1]+psA[r1][c1];};
        vector<Move> res;
        for(int r1=0;r1<R;r1++)for(int r2=r1;r2<R;r2++)for(int c1=0;c1<C;c1++)for(int c2=c1;c2<C;c2++){
            if(rV(r1,c1,r2,c2)!=10)continue;
            if(rA(r1,c1,r1,c2)==0||rA(r2,c1,r2,c2)==0||rA(r1,c1,r2,c1)==0||rA(r1,c2,r2,c2)==0)continue;
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
    void applyMove(const Move&m,int player){
        if(m.isPass()){lastPass=true;return;}
        for(int r=m.r1;r<=m.r2;r++)for(int c=m.c1;c<=m.c2;c++){alive[r][c]=false;owner[r][c]=player;}
        lastPass=false;
    }
    void applyCoords(int r1,int c1,int r2,int c2,int player){
        Move m;m.r1=r1;m.c1=c1;m.r2=r2;m.c2=c2; applyMove(m,player);
        if(player==opp) lastOpp=m;
    }
    Move passMove() const { return Move{}; }
    long long topGainSum(const vector<Move>&v,int k) const { long long s=0; for(int i=0;i<min(k,(int)v.size());i++)s+=v[i].gain; return s; }
    long long topStealSum(const vector<Move>&v,int k) const { long long s=0; for(int i=0;i<min(k,(int)v.size());i++)s+=v[i].stolen; return s; }

    // bot_strong 평가 그대로
    long long evaluate(int player) const {
        int e=3-player;
        vector<Move> mine=getCandidates(player), they=getCandidates(e);
        int diff=diffScore(player);
        long long myG1=mine.empty()?0:mine[0].gain, opG1=they.empty()?0:they[0].gain;
        long long myG3=topGainSum(mine,3), opG3=topGainSum(they,3);
        long long myS3=topStealSum(mine,3), opS3=topStealSum(they,3);
        long long s=0;
        s+=120000LL*diff;
        s+=14000LL*myG1; s+=7000LL*myG3; s+=9000LL*myS3;
        s-=23000LL*opG1; s-=15000LL*opG3; s-=16000LL*opS3;
        s+=100LL*((int)mine.size()-(int)they.size());
        return s;
    }

    // 포커스 후보: 직전 수(lastMv)와 겹치는 후보 + 안전용 글로벌 상위 K
    vector<Move> pickFocused(int player, const Move& lastMv, int beam, bool safeSort) const {
        vector<Move> all=getCandidates(player);
        if(all.empty()) return {};
        vector<Move> pool; set<tuple<int,int,int,int>> used;
        auto add=[&](const Move&m){auto k=make_tuple(m.r1,m.c1,m.r2,m.c2); if(used.insert(k).second)pool.push_back(m);};
        // 안전용 글로벌 상위 K (비국소 큰 수 놓치지 않게)
        for(int i=0;i<min(SAFE_K,(int)all.size());i++) add(all[i]);
        // 직전 수에 영향받는(겹치는) 후보
        if(!lastMv.isPass()){
            for(const Move&m:all) if(bboxOverlap(m,lastMv)) add(m);
        } else {
            // 직전 수 정보 없으면(게임 시작 등) 글로벌 상위로 채움
            for(int i=0;i<min(beam,(int)all.size());i++) add(all[i]);
        }
        if(safeSort){
            int e=3-player;
            for(Move&m:pool){
                Game nx=*this; nx.applyMove(m,player);
                vector<Move> op=nx.getCandidates(e);
                long long o1=op.empty()?0:op[0].gain, o3=nx.topGainSum(op,3), s3=nx.topStealSum(op,3);
                m.searchScore=150000LL*nx.diffScore(player)+76000LL*m.gain+56000LL*m.stolen
                             +12000LL*m.area-115000LL*o1-52000LL*o3-58000LL*s3;
            }
            sort(pool.begin(),pool.end(),[](const Move&a,const Move&b){
                if(a.searchScore!=b.searchScore)return a.searchScore>b.searchScore;
                return a.orderScore>b.orderScore;});
        } else {
            sort(pool.begin(),pool.end(),[](const Move&a,const Move&b){
                if(a.orderScore!=b.orderScore)return a.orderScore>b.orderScore;
                return a.area>b.area;});
        }
        if((int)pool.size()>beam) pool.resize(beam);
        return pool;
    }

    long long negamax(Game st,int player,int depth,bool prevPass,int beam,
                      long long alpha,long long beta,const Move& lastMv) const {
        if(depth<=0||outOfTime()) return st.evaluate(player);
        vector<Move> moves=st.pickFocused(player,lastMv,beam,false);
        if(st.occupiedCount()>0) moves.push_back(st.passMove());
        if(moves.empty()) return st.evaluate(player);
        long long best=LLONG_MIN/4; int nb=beam;   // 포커스라 분기 적음 → beam 유지
        for(const Move&m:moves){
            long long sc;
            if(m.isPass()){
                if(prevPass) sc=st.terminalScore(player);
                else{Game nx=st;nx.applyMove(m,player);sc=-negamax(nx,3-player,depth-1,true,nb,-beta,-alpha,m);}
            } else {Game nx=st;nx.applyMove(m,player);sc=-negamax(nx,3-player,depth-1,false,nb,-beta,-alpha,m);}
            best=max(best,sc); alpha=max(alpha,sc);
            if(alpha>=beta)break;
            if(timeUp)break;
        }
        return best;
    }
    Move chooseMove(int timeLeft){
        auto t0=chrono::steady_clock::now();
        long long budget=timeLeft/8; budget=min<long long>(budget,(long long)timeLeft-500); budget=max<long long>(budget,20);
        deadline=t0+chrono::milliseconds(budget); timeUp=false; nodeCnt=0;
        int beam=12;
        vector<Move> root=pickFocused(me,lastOpp,beam,true);
        if(occupiedCount()>0) root.push_back(passMove());
        if(root.empty()) return passMove();
        Move best=root[0];
        int maxDepth=FIXED_DEPTH>0?FIXED_DEPTH:20, startDepth=FIXED_DEPTH>0?FIXED_DEPTH:1;
        for(int depth=startDepth;depth<=maxDepth;depth++){
            Move lb=root[0]; long long bs=LLONG_MIN/4; bool done=true;
            for(const Move&m:root){
                long long sc;
                if(m.isPass()){
                    if(lastPass)sc=terminalScore(me);
                    else{Game nx=*this;nx.applyMove(m,me);sc=-negamax(nx,opp,depth-1,true,beam,LLONG_MIN/4,LLONG_MAX/4,m);}
                } else {Game nx=*this;nx.applyMove(m,me);sc=-negamax(nx,opp,depth-1,false,beam,LLONG_MIN/4,LLONG_MAX/4,m);}
                if(timeUp){done=false;break;}
                if(sc>bs||(sc==bs&&m.orderScore>lb.orderScore)){bs=sc;lb=m;}
            }
            if(!done)break;
            best=lb;
            if(FIXED_DEPTH>0)break;
            auto it=find_if(root.begin(),root.end(),[&](const Move&x){return x.r1==best.r1&&x.c1==best.c1&&x.r2==best.r2&&x.c2==best.c2;});
            if(it!=root.end())rotate(root.begin(),it,it+1);
            if(chrono::steady_clock::now()>=deadline)break;
        }
        return best;
    }
};

int main(){
    ios::sync_with_stdio(false); cin.tie(nullptr);
    if(const char* d=getenv("FIXED_DEPTH")) FIXED_DEPTH=atoi(d);
    if(const char* k=getenv("SAFE_K")) SAFE_K=atoi(k);
    Game game; string cmd;
    while(cin>>cmd){
        if(cmd=="READY"){string o;cin>>o;if(o=="FIRST"){game.me=1;game.opp=2;}else{game.me=2;game.opp=1;}cout<<"OK"<<endl;}
        else if(cmd=="INIT"){vector<string> rows(10);for(int i=0;i<10;i++)cin>>rows[i];game.init(rows);}
        else if(cmd=="TIME"){int t1,t2;cin>>t1>>t2;Move m=game.chooseMove(t1);game.applyMove(m,game.me);
            cout<<m.r1<<' '<<m.c1<<' '<<m.r2<<' '<<m.c2<<endl;}
        else if(cmd=="OPP"){int r1,c1,r2,c2,t;cin>>r1>>c1>>r2>>c2>>t;game.applyCoords(r1,c1,r2,c2,game.opp);}
        else if(cmd=="FINISH")break;
    }
}
