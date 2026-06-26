// 작은 MLP 평가 봇 (자가대국 ES 최적화용).
//   실행: ./bot_mlp [weights.bin]
//   - 평가 = MLP(집계 feature 12개) : 12 -> H -> 1, ReLU, 선형출력. 가벼워 실시간에 빠름.
//   - 가중치 없으면 zero-net(=이동순서 휴리스틱). weights.bin = float32 flat [W1(H*IN),b1(H),W2(H),b2(1)].
//   - 탐색/후보선별은 bot_strong 과 동일. FIXED_DEPTH 환경변수 지원.
//   - feature 는 병합 스캔(직사각형 1회 스캔)으로 계산: getCandidates 미사용 → 빠름.
#include <bits/stdc++.h>
using namespace std;

static const int R = 10, C = 17;
static const int IN = 12;
static const int H = 28;   // ~400 파라미터
int FIXED_DEPTH = 0;

struct Net {
    float W1[H*IN], b1[H], W2[H], b2;
    bool loaded=false;
    Net(){ memset(W1,0,sizeof(W1)); memset(b1,0,sizeof(b1)); memset(W2,0,sizeof(W2)); b2=0; }
    bool loadBin(const char* p){
        FILE* f=fopen(p,"rb"); if(!f)return false;
        size_t n=0;
        n+=fread(W1,sizeof(float),H*IN,f);
        n+=fread(b1,sizeof(float),H,f);
        n+=fread(W2,sizeof(float),H,f);
        n+=fread(&b2,sizeof(float),1,f);
        fclose(f);
        loaded = (n == (size_t)(H*IN+H+H+1));
        return loaded;
    }
    float forward(const float* x) const {
        float h[H];
        for(int j=0;j<H;j++){ float a=b1[j]; const float* w=&W1[j*IN]; for(int i=0;i<IN;i++)a+=w[i]*x[i]; h[j]=a>0?a:0; }
        float o=b2; for(int j=0;j<H;j++)o+=W2[j]*h[j];
        return o;
    }
};
Net NET;

struct Move {
    int r1=-1,c1=-1,r2=-1,c2=-1;
    int area=0, neutral=0, stolen=0, own=0, gain=0;
    long long orderScore=0, searchScore=0;
    bool isPass() const { return r1<0; }
};

struct Game {
    int val[R][C]{}; bool alive[R][C]{}; int owner[R][C]{};
    int me=1, opp=2; bool lastPass=false;
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
        memset(owner,0,sizeof(owner)); lastPass=false;
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
        if(d>0) return 1000000000LL+1000000LL*d;
        if(d==0) return 0;
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
    int overlapAlive(const Move&a,const Move&b) const {
        if(a.isPass()||b.isPass())return 0;
        int r1=max(a.r1,b.r1),c1=max(a.c1,b.c1),r2=min(a.r2,b.r2),c2=min(a.c2,b.c2);
        if(r1>r2||c1>c2)return 0;
        int cnt=0; for(int r=r1;r<=r2;r++)for(int c=c1;c<=c2;c++)if(alive[r][c])cnt++;
        return cnt;
    }
    void applyMove(const Move&m,int player){
        if(m.isPass()){lastPass=true;return;}
        for(int r=m.r1;r<=m.r2;r++)for(int c=m.c1;c<=m.c2;c++){alive[r][c]=false;owner[r][c]=player;}
        lastPass=false;
    }
    void applyCoords(int r1,int c1,int r2,int c2,int player){ Move m;m.r1=r1;m.c1=c1;m.r2=r2;m.c2=c2;applyMove(m,player); }
    Move passMove() const { return Move{}; }

    // 집계 feature 12개 (병합 스캔)
    void features(int player, float* f) const {
        int e=3-player; int aliveCnt=0, meC=0, opC=0;
        int psV[R+1][C+1]{}, psA[R+1][C+1]{}, pMe[R+1][C+1]{}, pOp[R+1][C+1]{};
        for(int r=0;r<R;r++)for(int c=0;c<C;c++){
            if(alive[r][c])aliveCnt++;
            if(owner[r][c]==player)meC++; else if(owner[r][c]==e)opC++;
            int av=alive[r][c]?1:0, v=av?val[r][c]:0;
            int mm=(owner[r][c]==player)?1:0, oo=(owner[r][c]==e)?1:0;
            psV[r+1][c+1]=psV[r][c+1]+psV[r+1][c]-psV[r][c]+v;
            psA[r+1][c+1]=psA[r][c+1]+psA[r+1][c]-psA[r][c]+av;
            pMe[r+1][c+1]=pMe[r][c+1]+pMe[r+1][c]-pMe[r][c]+mm;
            pOp[r+1][c+1]=pOp[r][c+1]+pOp[r+1][c]-pOp[r][c]+oo;
        }
        auto Q=[&](int p[R+1][C+1],int r1,int c1,int r2,int c2){return p[r2+1][c2+1]-p[r1][c2+1]-p[r2+1][c1]+p[r1][c1];};
        auto push3=[](int&a,int&b,int&c,int x){ if(x>a){c=b;b=a;a=x;} else if(x>b){c=b;b=x;} else if(x>c)c=x; };
        int cnt=0, mg1=0,mg2=0,mg3=0, og1=0,og2=0,og3=0, ms1=0,ms2=0,ms3=0, os1=0,os2=0,os3=0;
        for(int r1=0;r1<R;r1++)for(int r2=r1;r2<R;r2++)for(int c1=0;c1<C;c1++)for(int c2=c1;c2<C;c2++){
            if(Q(psV,r1,c1,r2,c2)!=10)continue;
            if(Q(psA,r1,c1,r1,c2)==0||Q(psA,r2,c1,r2,c2)==0||Q(psA,r1,c1,r2,c1)==0||Q(psA,r1,c2,r2,c2)==0)continue;
            int n=Q(psA,r1,c1,r2,c2), a=Q(pMe,r1,c1,r2,c2), b=Q(pOp,r1,c1,r2,c2);
            cnt++;
            push3(mg1,mg2,mg3,n+2*b); push3(og1,og2,og3,n+2*a);
            push3(ms1,ms2,ms3,b);     push3(os1,os2,os3,a);
        }
        f[0]=(meC-opC)/50.0f;
        f[1]=(meC+opC)/170.0f;
        f[2]=aliveCnt/170.0f;
        f[3]=cnt/60.0f;
        f[4]=mg1/20.0f;
        f[5]=mg2/20.0f;
        f[6]=(mg1+mg2+mg3)/40.0f;
        f[7]=og1/20.0f;
        f[8]=og2/20.0f;
        f[9]=(og1+og2+og3)/40.0f;
        f[10]=(ms1+ms2+ms3)/20.0f;
        f[11]=(os1+os2+os3)/20.0f;
    }
    long long evaluate(int player) const {
        float f[IN]; features(player,f);
        return (long long)llround((double)NET.forward(f)*1000000.0);
    }

    // 탈취 교환 quiescence (ES 평가 위에서). 방금 놓인 칸 되먹는 교환을 끝까지 추적.
    long long qsearch(int player, long long alpha, long long beta, int qd) const {
        long long stand = evaluate(player);
        if(qd<=0||outOfTime()) return stand;
        if(stand>=beta) return stand;
        if(stand>alpha) alpha=stand;
        vector<Move> cands=getCandidates(player);
        long long best=stand; int cnt=0;
        for(const Move&m:cands){
            if(m.stolen<=0) continue;          // 탈취 수만(2배 교환)
            Game nx=*this; nx.applyMove(m,player);
            long long sc=-nx.qsearch(3-player,-beta,-alpha,qd-1);
            if(sc>best)best=sc;
            if(sc>alpha)alpha=sc;
            if(alpha>=beta)break;
            if(++cnt>=4)break;
        }
        return best;
    }

    vector<Move> pickMoves(int player,int beam,bool safeSort) const {
        vector<Move> all=getCandidates(player);
        if(all.empty())return{};
        int e=3-player;
        vector<Move> enemyMoves=getCandidates(e);
        vector<Move> pool; set<tuple<int,int,int,int>> used;
        auto addMove=[&](const Move&m){auto k=make_tuple(m.r1,m.c1,m.r2,m.c2);if(used.insert(k).second)pool.push_back(m);};
        auto addTopBy=[&](auto cmp,int cnt){ vector<Move> tmp=all; sort(tmp.begin(),tmp.end(),cmp);
            for(int i=0;i<min(cnt,(int)tmp.size());i++)addMove(tmp[i]); };
        if((int)all.size()<=max(beam*2,32)){ for(const Move&m:all)addMove(m); }
        else {
            int wide=max(beam*3,45);
            for(int i=0;i<min(wide,(int)all.size());i++)addMove(all[i]);
            addTopBy([](const Move&a,const Move&b){
                if(a.stolen!=b.stolen)return a.stolen>b.stolen;
                if(a.gain!=b.gain)return a.gain>b.gain;
                return a.area>b.area;},beam);
            addTopBy([](const Move&a,const Move&b){
                if(a.area!=b.area)return a.area>b.area;
                if(a.gain!=b.gain)return a.gain>b.gain;
                return a.stolen>b.stolen;},beam);
        }
        if(!enemyMoves.empty()){
            vector<pair<long long,int>> br; int T=min(14,(int)enemyMoves.size());
            for(int i=0;i<(int)all.size();i++){ long long bs=0;
                for(int j=0;j<T;j++){ int ov=overlapAlive(all[i],enemyMoves[j]); if(ov==0)continue;
                    bs+=1LL*ov*(1000LL*enemyMoves[j].gain+950LL*enemyMoves[j].stolen+130LL*enemyMoves[j].area); }
                bs+=3500LL*all[i].gain+5500LL*all[i].stolen; br.push_back({bs,i}); }
            sort(br.begin(),br.end(),greater<>());
            for(int i=0;i<min(beam*2,(int)br.size());i++)addMove(all[br[i].second]);
        }
        if(safeSort){
            for(Move&m:pool){ Game nx=*this; nx.applyMove(m,player);
                vector<Move> op=nx.getCandidates(e);
                long long o1=op.empty()?0:op[0].gain, o3=0,s3=0;
                for(int i=0;i<min(3,(int)op.size());i++){o3+=op[i].gain;s3+=op[i].stolen;}
                m.searchScore=150000LL*nx.diffScore(player)+76000LL*m.gain+56000LL*m.stolen
                             +12000LL*m.area-115000LL*o1-52000LL*o3-58000LL*s3; }
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
        if(depth<=0||outOfTime())return st.qsearch(player,alpha,beta,4);   // 잎에서 탈취 quiescence
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
            if(alpha>=beta)break;
            if(timeUp)break;
        }
        return best;
    }
    Move chooseMove(int timeLeft){
        auto t0=chrono::steady_clock::now();
        long long budget=timeLeft/8; budget=min<long long>(budget,(long long)timeLeft-500); budget=max<long long>(budget,20);
        deadline=t0+chrono::milliseconds(budget); timeUp=false; nodeCnt=0;
        int myCnt=(int)getCandidates(me).size();
        int beam=16; if(myCnt<=26)beam=14; if(myCnt<=18)beam=12; if(myCnt<=10)beam=10;
        vector<Move> root=pickMoves(me,beam,true);
        if(occupiedCount()>0)root.push_back(passMove());
        if(root.empty())return passMove();
        Move best=root[0];
        int maxDepth=FIXED_DEPTH>0?FIXED_DEPTH:14, startDepth=FIXED_DEPTH>0?FIXED_DEPTH:1;
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

int main(int argc,char**argv){
    ios::sync_with_stdio(false); cin.tie(nullptr);
    NET.loadBin(argc>1 ? argv[1] : "data.bin");   // 가중치: 인자 없으면 같은 폴더 data.bin
    if(const char* d=getenv("FIXED_DEPTH")) FIXED_DEPTH=atoi(d);
    if(const char* m=getenv("MLP_DEPTH")) FIXED_DEPTH=atoi(m);  // 내 봇 전용 깊이(비대칭 학습)
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
