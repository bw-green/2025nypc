// 10x17 합10 땅따먹기 봇 — 학습 데이터 기반 "후보 수 평가(LinearMoveEvaluator)" 추가.
//
//  설계 원칙:
//   - AI는 r1c1r2c2 를 직접 예측하지 않는다. 기존 getCandidates/applyMove/negamax/chooseMove 유지.
//   - 모델은 "생성된 후보 수들 중 어떤 수가 좋은지 점수만" 매긴다(이동순서/선별).
//   - 단일 C++17 파일, 외부 라이브러리 없음. 학습은 오프라인, 제출 코드엔 weight만 하드코딩.
//
//  두 가지 빌드:
//   - 제출 모드(기본):  g++ -std=c++17 -O2 -o bot bot_submit.cpp
//       READY/INIT/TIME/OPP/FINISH 프로토콜. 학습 코드 영향 없음.
//   - 학습 데이터 추출:  g++ -std=c++17 -O2 -DLOCAL_TRAIN -o train_extract bot_submit.cpp
//       expert game log(들)을 파싱 → 매 턴 후보별 feature CSV 출력(stdin 또는 argv 파일들).
//       CSV: game_id,ply,player,candidate_index,is_expert,f0,...,f16
//
//  학습 절차(권장):
//   1) train_extract 로 CSV 생성  →  2) Python 등에서 imitation learning(예: expert 후보가 1등이 되도록
//      가중치 학습)  →  3) 얻은 weight 를 아래 DEFAULT_WEIGHTS 에 넣어 재컴파일  →  4) self-play 로 미세튜닝.
#include <climits>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <set>
#include <tuple>
#include <chrono>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cctype>
using namespace std;

// ================== 후보 수 feature / 선형 평가기 ==================
static const int FEATURE_COUNT = 17;
//  f0  immediate gain (neutral + 2*stolen)
//  f1  neutral count
//  f2  stolen count
//  f3  own count
//  f4  area
//  f5  이 수 후 diffScore(player)
//  f6  이 수 후 내 후보 수
//  f7  이 수 후 상대 후보 수
//  f8  이 수 후 내 best gain
//  f9  이 수 후 상대 best gain
//  f10 이 수 후 내 top3 gain sum
//  f11 이 수 후 상대 top3 gain sum
//  f12 이 수 후 내 top3 stolen sum
//  f13 이 수 후 상대 top3 stolen sum
//  f14 blockScore (상대의 현재 위험 후보와 겹쳐 막는 정도)
//  f15 pass 여부
//  f16 endgame 여부
struct EvalWeights { double w[FEATURE_COUNT]; };

// 초기 weight: 기존 orderScore / searchScore / evaluate 의 감각을 반영(= bot_strong 수준에서 출발).
// self-play / imitation 으로 얻은 weight 로 이 배열만 교체하면 된다.
// === imitation learning(자가대국 데이터 400게임)으로 학습된 weight ===
//   held-out 수예측 top-1 40.7% / top-3 68.3% (과적합 없음). self-play 로 추가 튜닝 가능.
//   (원시 feature 기준. 점수는 랭킹용이라 스케일 절대값은 무의미.)
static EvalWeights DEFAULT_WEIGHTS = {{
    /*gain*/            0.248857,
    /*neutral*/         0.80199,
    /*stolen*/          0.273415,
    /*own*/            -0.289397,
    /*area*/            0.256248,
    /*diff_after*/      0.0798584,
    /*mycand_after*/   -0.0358917,
    /*opcand_after*/   -0.0358917,
    /*mybest_after*/   -0.129167,
    /*opbest_after*/   -0.264209,
    /*mytop3gain*/      0.122224,
    /*optop3gain*/      0.256534,
    /*mytop3steal*/     0.0502917,
    /*optop3steal*/    -0.934006,
    /*block*/           4.38349e-07,
    /*is_pass*/         0.813024,
    /*is_endgame*/      0.00785602
}};

static EvalWeights G_WEIGHTS = DEFAULT_WEIGHTS;   // 런타임에 교체 가능

struct LinearMoveEvaluator {
    const EvalWeights* W = &G_WEIGHTS;
    double score(const double* f) const {
        double s = 0.0;
        for (int i = 0; i < FEATURE_COUNT; i++) s += W->w[i] * f[i];
        return s;
    }
};
static LinearMoveEvaluator EVALU;

int FIXED_DEPTH = 0;

struct Move {
    int r1=-1,c1=-1,r2=-1,c2=-1;
    int area=0, neutral=0, stolen=0, own=0, gain=0;
    long long orderScore=0, searchScore=0;   // searchScore = learned score(정렬용)
    bool isPass() const { return r1 < 0; }
};

struct Game; // 전방 선언
void extractMoveFeatures(const Game& st, const Move& m, int player, double* f);

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
        if ((++nodeCnt & 255)==0)
            if (chrono::steady_clock::now()>=deadline) timeUp=true;
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
    int aliveCount() const { int n=0; for(int r=0;r<R;r++)for(int c=0;c<C;c++)if(alive[r][c])n++; return n; }
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
    long long topGainSum(const vector<Move>&v,int k) const { long long s=0; for(int i=0;i<min(k,(int)v.size());i++)s+=v[i].gain; return s; }
    long long topStealSum(const vector<Move>&v,int k) const { long long s=0; for(int i=0;i<min(k,(int)v.size());i++)s+=v[i].stolen; return s; }

    // 잎 노드 정적 평가(기존 유지). 학습 평가기는 '수 선별/정렬'용이고 이건 별개.
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

    // 탈취 교환 quiescence: 잎에서 '상대 칸을 먹는 수(stolen>0)'만 따라 교환이 끝날 때까지 더 봄.
    //   "놓자마자 되먹히는" 함정 제거. 깊이/분기 제한으로 타임아웃 방지.
    long long qsearch(int player, long long alpha, long long beta, int qd) const {
        long long stand=evaluate(player);
        if(qd<=0||outOfTime()) return stand;
        if(stand>=beta) return stand;
        if(stand>alpha) alpha=stand;
        vector<Move> cands=getCandidates(player);
        long long best=stand; int cnt=0;
        for(const Move&m:cands){
            if(m.stolen<=0) continue;
            Game nx=*this; nx.applyMove(m,player);
            long long sc=-nx.qsearch(3-player,-beta,-alpha,qd-1);
            if(sc>best) best=sc;
            if(sc>alpha) alpha=sc;
            if(alpha>=beta) break;
            if(++cnt>=4) break;
        }
        return best;
    }

    // === 학습된 평가기로 후보를 점수화/선별 ===
    // pool = (orderScore 상위 wide) + (steal상위) + (area상위) + (상대 위험수 차단 후보)
    // safeSort=true 면 learned score 로 재정렬(상위 = learned 상위가 pool 앞으로).
    vector<Move> pickMoves(int player,int beam,bool safeSort) const {
        vector<Move> all=getCandidates(player);
        if(all.empty())return{};
        int e=3-player;
        vector<Move> enemyMoves=getCandidates(e);
        vector<Move> pool; set<tuple<int,int,int,int>> used;
        auto addMove=[&](const Move&m){auto k=make_tuple(m.r1,m.c1,m.r2,m.c2);if(used.insert(k).second)pool.push_back(m);};
        auto addTopBy=[&](auto cmp,int cnt){ vector<Move> tmp=all; sort(tmp.begin(),tmp.end(),cmp);
            for(int i=0;i<min(cnt,(int)tmp.size());i++)addMove(tmp[i]); };
        if((int)all.size()<=max(beam*2,32)){
            for(const Move&m:all)addMove(m);
        } else {
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
            // 학습된 선형 평가기로 각 후보 점수화 → 정렬(= 후보 선별에 learned score 반영)
            for(Move&m:pool){
                double f[FEATURE_COUNT];
                extractMoveFeatures(*this,m,player,f);
                m.searchScore=(long long)llround(EVALU.score(f));
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
        if(depth<=0||outOfTime())return st.qsearch(player,alpha,beta,4);   // 잎: 탈취 quiescence
        vector<Move> moves=st.pickMoves(player,beam,false);   // 내부 노드는 빠른 orderScore 정렬
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
        vector<Move> root=pickMoves(me,beam,true);   // 루트는 learned score 선별
        // pass 로직: occupiedCount()==0(첫 턴)이면 pass 제외, 그 외 허용
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
                if(sc>bs||(sc==bs&&m.searchScore>lb.searchScore)){bs=sc;lb=m;}
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

// ================== 후보 수 feature 추출 (학습/추론 공용) ==================
void extractMoveFeatures(const Game& st, const Move& m, int player, double* f){
    for(int i=0;i<FEATURE_COUNT;i++) f[i]=0.0;
    int e=3-player;
    f[15] = m.isPass() ? 1.0 : 0.0;
    f[16] = (st.aliveCount() <= 50) ? 1.0 : 0.0;   // endgame 여부(생존 칸 적으면)
    if(m.isPass()){
        f[5] = (double)st.diffScore(player);        // pass 는 보드 불변
        return;
    }
    f[0]=m.gain; f[1]=m.neutral; f[2]=m.stolen; f[3]=m.own; f[4]=m.area;
    // blockScore: 상대의 현재 위험 후보와 겹쳐 막는 정도
    {
        vector<Move> em=st.getCandidates(e);
        int T=min(14,(int)em.size());
        double bs=0.0;
        for(int j=0;j<T;j++){
            int ov=st.overlapAlive(m,em[j]); if(ov==0)continue;
            bs += (double)ov*(1000.0*em[j].gain + 950.0*em[j].stolen + 130.0*em[j].area);
        }
        f[14]=bs;
    }
    // 이 수를 둔 뒤 상태
    Game nx=st; nx.applyMove(m,player);
    f[5]=(double)nx.diffScore(player);
    vector<Move> myc=nx.getCandidates(player), opc=nx.getCandidates(e);
    f[6]=(double)myc.size(); f[7]=(double)opc.size();
    f[8]=myc.empty()?0.0:(double)myc[0].gain;
    f[9]=opc.empty()?0.0:(double)opc[0].gain;
    f[10]=(double)nx.topGainSum(myc,3); f[11]=(double)nx.topGainSum(opc,3);
    f[12]=(double)nx.topStealSum(myc,3); f[13]=(double)nx.topStealSum(opc,3);
}

#ifdef LOCAL_TRAIN
static const char* FEATURE_NAMES[FEATURE_COUNT] = {
    "gain","neutral","stolen","own","area","diff_after","mycand_after","opcand_after",
    "mybest_after","opbest_after","mytop3gain_after","optop3gain_after",
    "mytop3steal_after","optop3steal_after","block","is_pass","is_endgame"
};
// ================== 학습용: expert game log → 후보별 feature CSV ==================
//   입력: argv 파일들(없으면 stdin). 여러 게임(INIT 여러 번) 연속 가능.
//   로그 형식(유연 파싱): ... INIT <10개 17자리 행> ... FIRST r1 c1 r2 c2 [time] / SECOND ... ... FINISH
//   출력(stdout): game_id,ply,player,candidate_index,is_expert,f0..f16
static bool isNum(const string& s){
    if(s.empty())return false;
    for(char c:s) if(!(isdigit((unsigned char)c)||c=='-')) return false;
    return true;
}
static void emitState(int gid,int ply,int player,const Game& st,const Move& expert){
    vector<Move> cands=st.getCandidates(player);
    if(st.occupiedCount()>0) cands.push_back(st.passMove());  // pass 후보 포함
    for(int idx=0; idx<(int)cands.size(); idx++){
        const Move& m=cands[idx];
        bool isExpert = (m.r1==expert.r1&&m.c1==expert.c1&&m.r2==expert.r2&&m.c2==expert.c2);
        double f[FEATURE_COUNT];
        extractMoveFeatures(st,m,player,f);
        printf("%d,%d,%d,%d,%d",gid,ply,player,idx,isExpert?1:0);
        for(int i=0;i<FEATURE_COUNT;i++) printf(",%.6g",f[i]);
        printf("\n");
    }
}
int main(int argc,char**argv){
    // 입력 전체를 토큰화
    vector<string> tok;
    auto readStream=[&](istream& in){ string s; while(in>>s) tok.push_back(s); };
    if(argc>1){ for(int a=1;a<argc;a++){ ifstream fin(argv[a]); if(fin) readStream(fin); } }
    else readStream(cin);

    // CSV 헤더
    printf("game_id,ply,player,candidate_index,is_expert");
    for(int i=0;i<FEATURE_COUNT;i++) printf(",%s",FEATURE_NAMES[i]);
    printf("\n");

    int gid=-1, ply=0; Game st; bool active=false;
    size_t i=0, n=tok.size();
    while(i<n){
        string t=tok[i++];
        if(t=="INIT"){
            if(i+10<=n){
                vector<string> rows; for(int k=0;k<10;k++) rows.push_back(tok[i++]);
                st.init(rows); gid++; ply=0; active=true;
            }
        } else if(t=="FIRST"||t=="SECOND"){
            if(!active){ continue; }
            int player=(t=="FIRST")?1:2;
            if(i+4>n) break;
            int r1=atoi(tok[i++].c_str()),c1=atoi(tok[i++].c_str());
            int r2=atoi(tok[i++].c_str()),c2=atoi(tok[i++].c_str());
            while(i<n && isNum(tok[i])) i++;   // 뒤따르는 time 등 숫자 토큰 스킵
            Move expert; expert.r1=r1;expert.c1=c1;expert.r2=r2;expert.c2=c2;
            emitState(gid,ply,player,st,expert);
            st.applyMove(expert,player);
            ply++;
        } else if(t=="FINISH"){
            active=false;
        } else if(t=="SCOREFIRST"||t=="SCORESECOND"||t=="READY"||t=="TIME"||t=="OPP"){
            if(i<n && isNum(tok[i])) i++;       // 뒤 숫자 스킵(있으면)
        }
    }
    return 0;
}
#else
// ================== 제출 모드: 표준 프로토콜 ==================
//  (학습용) argv[1] 에 weight 파일(17개 실수, 공백/줄 구분)을 주면 DEFAULT_WEIGHTS 대신 사용.
//  → ES self-play 가 재컴파일 없이 후보 weight 를 시험할 수 있음. 최종 제출은 인자 없이 DEFAULT_WEIGHTS.
static void loadWeights(const char* path){
    ifstream f(path); if(!f) return;
    for(int i=0;i<FEATURE_COUNT;i++){ double x; if(f>>x) G_WEIGHTS.w[i]=x; }
}
int main(int argc,char**argv){
    ios::sync_with_stdio(false); cin.tie(nullptr);
    if(const char* d=getenv("FIXED_DEPTH")) FIXED_DEPTH=atoi(d);
    G_WEIGHTS = DEFAULT_WEIGHTS;
    if(argc>1) loadWeights(argv[1]);   // 학습 시 후보 weight 로드
    Game game; string cmd;
    while(cin>>cmd){
        if(cmd=="READY"){string o;cin>>o;if(o=="FIRST"){game.me=1;game.opp=2;}else{game.me=2;game.opp=1;}cout<<"OK"<<endl;}
        else if(cmd=="INIT"){vector<string> rows(10);for(int i=0;i<10;i++)cin>>rows[i];game.init(rows);}
        else if(cmd=="TIME"){int t1,t2;cin>>t1>>t2;Move m=game.chooseMove(t1);game.applyMove(m,game.me);
            cout<<m.r1<<' '<<m.c1<<' '<<m.r2<<' '<<m.c2<<endl;}
        else if(cmd=="OPP"){int r1,c1,r2,c2,t;cin>>r1>>c1>>r2>>c2>>t;game.applyCoords(r1,c1,r2,c2,game.opp);}
        else if(cmd=="FINISH")break;
    }
    return 0;
}
#endif
