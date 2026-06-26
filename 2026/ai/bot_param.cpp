// 가중치를 파일에서 읽는 경량 eval 봇.
//   실행: ./bot_param [weights_file]
//   weights_file 없으면 기본값 사용 (단독 실행 가능).
//   환경변수 FIXED_DEPTH=k 가 있으면 iterative deepening 대신 정확히 k depth만 탐색
//   (학습 시 게임을 빠르고 재현가능하게 만들기 위함).
//
// eval = W0*diff + W1*myBest1 + W2*myTop3 + W3*opBest1 + W4*opTop3 + W5*mobility
//   - 한 번의 prefix-sum 스캔으로 모든 feature 계산 (벡터/정렬/Move생성 없음 = 비용↓)
#include <bits/stdc++.h>
using namespace std;

static const int NW = 6;
double W[NW] = {120000, 16000, 9000, -17000, -10000, 100};  // 기본 가중치
int FIXED_DEPTH = 0;

struct Move {
    int r1=-1,c1=-1,r2=-1,c2=-1;
    int area=0, neutral=0, stolen=0, own=0, gain=0;
    long long orderScore=0, searchScore=0;
    bool isPass() const { return r1 < 0; }
};

struct Game {
    static constexpr int R=10, C=17;
    int val[R][C]{}; bool alive[R][C]{}; int owner[R][C]{};
    int me=1, opp=2; bool lastPass=false;
    mutable bool timeUp=false;
    mutable chrono::steady_clock::time_point deadline;
    mutable long long nodeCnt=0;

    bool outOfTime() const {
        if (FIXED_DEPTH>0) return false;
        if (timeUp) return true;
        if ((++nodeCnt & 2047)==0)
            if (chrono::steady_clock::now()>=deadline) timeUp=true;
        return timeUp;
    }
    void init(const vector<string>& rows){
        memset(owner,0,sizeof(owner)); lastPass=false;
        for(int r=0;r<R;r++)for(int c=0;c<C;c++){val[r][c]=rows[r][c]-'0';alive[r][c]=true;}
    }
    int diffScore(int player) const {
        int e=3-player,a=0,b=0;
        for(int r=0;r<R;r++)for(int c=0;c<C;c++){
            if(owner[r][c]==player)a++; else if(owner[r][c]==e)b++;
        }
        return a-b;
    }
    int occupiedCount() const {
        int n=0; for(int r=0;r<R;r++)for(int c=0;c<C;c++) if(owner[r][c]) n++; return n;
    }
    long long terminalScore(int player) const {
        int d=diffScore(player);
        if(d>0) return 1000000000LL+1000000LL*d;
        if(d==0) return 0;
        return -1000000000LL+1000000LL*d;
    }
    Move makeMove(int r1,int c1,int r2,int c2,int player) const {
        Move m; m.r1=r1;m.c1=c1;m.r2=r2;m.c2=c2;
        m.area=(r2-r1+1)*(c2-c1+1); int e=3-player;
        for(int r=r1;r<=r2;r++)for(int c=c1;c<=c2;c++){
            if(owner[r][c]==0)m.neutral++; else if(owner[r][c]==e)m.stolen++; else m.own++;
        }
        m.gain=m.neutral+2*m.stolen;
        m.orderScore=120000LL*m.gain+95000LL*m.stolen+9000LL*m.neutral+300LL*m.area-2000LL*m.own;
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
            if(a.r1!=b.r1)return a.r1<b.r1; if(a.c1!=b.c1)return a.c1<b.c1;
            if(a.r2!=b.r2)return a.r2<b.r2; return a.c2<b.c2;
        });
        return res;
    }
    void applyMove(const Move&m,int player){
        if(m.isPass()){lastPass=true;return;}
        for(int r=m.r1;r<=m.r2;r++)for(int c=m.c1;c<=m.c2;c++){alive[r][c]=false;owner[r][c]=player;}
        lastPass=false;
    }
    void applyCoords(int r1,int c1,int r2,int c2,int player){
        Move m;m.r1=r1;m.c1=c1;m.r2=r2;m.c2=c2;applyMove(m,player);
    }
    Move passMove() const { return Move{}; }

    // ===== 경량 파라미터 eval =====
    // 한 번의 rect 스캔으로 myTop3/opTop3/mobility 계산. O(1)/rect.
    long long evaluate(int player) const {
        int e=3-player;
        int psV[R+1][C+1]{}, psA[R+1][C+1]{}, pN[R+1][C+1]{}, pMe[R+1][C+1]{}, pOp[R+1][C+1]{};
        for(int r=0;r<R;r++)for(int c=0;c<C;c++){
            int v=alive[r][c]?val[r][c]:0, a=alive[r][c]?1:0;
            int n=(owner[r][c]==0)?1:0, mm=(owner[r][c]==player)?1:0, oo=(owner[r][c]==e)?1:0;
            psV[r+1][c+1]=psV[r][c+1]+psV[r+1][c]-psV[r][c]+v;
            psA[r+1][c+1]=psA[r][c+1]+psA[r+1][c]-psA[r][c]+a;
            pN [r+1][c+1]=pN [r][c+1]+pN [r+1][c]-pN [r][c]+n;
            pMe[r+1][c+1]=pMe[r][c+1]+pMe[r+1][c]-pMe[r][c]+mm;
            pOp[r+1][c+1]=pOp[r][c+1]+pOp[r+1][c]-pOp[r][c]+oo;
        }
        auto Q=[&](int p[R+1][C+1],int r1,int c1,int r2,int c2){
            return p[r2+1][c2+1]-p[r1][c2+1]-p[r2+1][c1]+p[r1][c1];
        };
        int myg1=0,myg2=0,myg3=0, opg1=0,opg2=0,opg3=0, mob=0;
        auto push3=[](int&a,int&b,int&c,int x){
            if(x>a){c=b;b=a;a=x;} else if(x>b){c=b;b=x;} else if(x>c){c=x;}
        };
        for(int r1=0;r1<R;r1++)for(int r2=r1;r2<R;r2++)for(int c1=0;c1<C;c1++)for(int c2=c1;c2<C;c2++){
            if(Q(psV,r1,c1,r2,c2)!=10)continue;
            if(Q(psA,r1,c1,r1,c2)==0||Q(psA,r2,c1,r2,c2)==0||
               Q(psA,r1,c1,r2,c1)==0||Q(psA,r1,c2,r2,c2)==0)continue;
            int neu=Q(pN,r1,c1,r2,c2), mc=Q(pMe,r1,c1,r2,c2), oc=Q(pOp,r1,c1,r2,c2);
            int mygain=neu+2*oc;   // 내 관점 이득
            int opgain=neu+2*mc;   // 상대 관점 이득
            push3(myg1,myg2,myg3,mygain);
            push3(opg1,opg2,opg3,opgain);
            mob++;
        }
        double diff=diffScore(player);
        double myTop3=myg1+myg2+myg3, opTop3=opg1+opg2+opg3;
        double s = W[0]*diff + W[1]*myg1 + W[2]*myTop3
                 + W[3]*opg1 + W[4]*opTop3 + W[5]*mob;
        return (long long)s;
    }

    vector<Move> pickMoves(int player,int beam,bool safeSort) const {
        vector<Move> all=getCandidates(player);
        if(all.empty())return{};
        int e=3-player;
        vector<Move> pool; set<tuple<int,int,int,int>> used;
        auto add=[&](const Move&m){auto k=make_tuple(m.r1,m.c1,m.r2,m.c2);if(used.insert(k).second)pool.push_back(m);};
        int lim=min((int)all.size(), max(beam*3,40));
        for(int i=0;i<lim;i++)add(all[i]);
        if(safeSort){
            for(Move&m:pool){
                Game nx=*this; nx.applyMove(m,player);
                long long opp1=0; vector<Move> op=nx.getCandidates(e);
                if(!op.empty())opp1=op[0].gain;
                m.searchScore=150000LL*nx.diffScore(player)+90000LL*m.gain
                             +72000LL*m.stolen+12000LL*m.area-85000LL*opp1;
            }
            sort(pool.begin(),pool.end(),[](const Move&a,const Move&b){
                if(a.searchScore!=b.searchScore)return a.searchScore>b.searchScore;
                return a.orderScore>b.orderScore;});
        } else {
            sort(pool.begin(),pool.end(),[](const Move&a,const Move&b){
                if(a.orderScore!=b.orderScore)return a.orderScore>b.orderScore;
                return a.area>b.area;});
        }
        if((int)pool.size()>beam)pool.resize(beam);
        return pool;
    }
    long long negamax(Game st,int player,int depth,bool prevPass,int beam,long long alpha,long long beta) const {
        if(depth<=0||outOfTime())return st.evaluate(player);
        vector<Move> moves=st.pickMoves(player,beam,false);
        if(st.occupiedCount()>0)moves.push_back(st.passMove());
        long long best=LLONG_MIN/4; int nb=max(5,beam-2);
        for(const Move&m:moves){
            long long sc;
            if(m.isPass()){
                if(prevPass)sc=st.terminalScore(player);
                else{Game nx=st;nx.applyMove(m,player);sc=-negamax(nx,3-player,depth-1,true,nb,-beta,-alpha);}
            } else {Game nx=st;nx.applyMove(m,player);sc=-negamax(nx,3-player,depth-1,false,nb,-beta,-alpha);}
            best=max(best,sc); alpha=max(alpha,sc);
            if(alpha>=beta)break; if(timeUp)break;
        }
        return best;
    }
    Move chooseMove(int timeLeft){
        auto t0=chrono::steady_clock::now();
        long long budget=timeLeft/8; budget=min<long long>(budget,(long long)timeLeft-200);
        budget=max<long long>(budget,50);
        deadline=t0+chrono::milliseconds(budget); timeUp=false; nodeCnt=0;
        int myCnt=(int)getCandidates(me).size();
        int beam=16; if(myCnt<=26)beam=14; if(myCnt<=18)beam=12; if(myCnt<=10)beam=10;
        vector<Move> root=pickMoves(me,beam,true);
        bool real=false; for(auto&m:root)if(!m.isPass()&&m.gain>0){real=true;break;}
        if(occupiedCount()>0&&!real)root.push_back(passMove());
        if(root.empty())return passMove();
        Move best=root[0];
        int maxDepth = FIXED_DEPTH>0 ? FIXED_DEPTH : 14;
        int startDepth = FIXED_DEPTH>0 ? FIXED_DEPTH : 1;
        for(int depth=startDepth;depth<=maxDepth;depth++){
            Move lb=root[0]; long long bs=LLONG_MIN/4; bool done=true;
            for(const Move&m:root){
                long long sc;
                if(m.isPass()){
                    if(lastPass)sc=terminalScore(me);
                    else{Game nx=*this;nx.applyMove(m,me);sc=-negamax(nx,opp,depth-1,true,max(5,beam-2),LLONG_MIN/4,LLONG_MAX/4);}
                } else {Game nx=*this;nx.applyMove(m,me);sc=-negamax(nx,opp,depth-1,false,max(5,beam-2),LLONG_MIN/4,LLONG_MAX/4);}
                if(timeUp){done=false;break;}
                if(sc>bs||(sc==bs&&m.orderScore>lb.orderScore)){bs=sc;lb=m;}
            }
            if(!done)break; best=lb;
            auto it=find_if(root.begin(),root.end(),[&](const Move&x){
                return x.r1==best.r1&&x.c1==best.c1&&x.r2==best.r2&&x.c2==best.c2;});
            if(it!=root.end())rotate(root.begin(),it,it+1);
            if(FIXED_DEPTH>0)break;
            if(chrono::steady_clock::now()>=deadline)break;
        }
        return best;
    }
};

void loadWeights(const char* path){
    ifstream f(path); if(!f)return;
    for(int i=0;i<NW;i++){ double x; if(f>>x) W[i]=x; }
}

int main(int argc,char**argv){
    ios::sync_with_stdio(false); cin.tie(nullptr);
    if(argc>1) loadWeights(argv[1]);
    if(const char* d=getenv("FIXED_DEPTH")) FIXED_DEPTH=atoi(d);
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
