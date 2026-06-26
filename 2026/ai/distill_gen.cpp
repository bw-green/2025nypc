// Distillation 데이터 생성기.
//   실행: ./distill_gen <out_file> <num_games> <seed> [epsilon]
//   - 랜덤 맵에서 (epsilon 확률 무작위 / 아니면 탐욕) 으로 대국을 진행하며,
//     매 위치에서 (nnue feature 522차원, bot_strong 의 정적 평가값을 squash한 타깃) 한 줄씩 기록.
//   - 깊은 탐색 없이 위치만 만들고 평가 1회 호출 → 매우 빠름. 대량 데이터 생성용.
//   - 목적: 신경망이 bot_strong 의 손튜닝 평가를 모방(distill)하도록 지도학습 데이터 제공.
//
// feature/정규화 상수는 nnue_bot.cpp 의 features() 와 반드시 동일해야 함.
#include <bits/stdc++.h>
using namespace std;

static const int R = 10, C = 17;
static const int BOARD = 3 * R * C;     // 510
static const int EXTRA = 10;            // nnue_bot.cpp 와 동일
static const int IN = BOARD + EXTRA;     // 520
static const double SCALE = 1000000.0;   // 평가값 squash 스케일 (nnue 추론 스케일과 동일)

struct Move {
    int r1=-1,c1=-1,r2=-1,c2=-1;
    int area=0, neutral=0, stolen=0, own=0, gain=0;
    long long orderScore=0;
    bool isPass() const { return r1 < 0; }
};

int LABEL_DEPTH = 0;   // >0이면 라벨을 depth-LABEL_DEPTH 미니맥스 값으로 (없으면 정적 평가)
int LABEL_BEAM = 8;

struct Game {
    int val[R][C]{}; bool alive[R][C]{}; int owner[R][C]{};

    void init(mt19937& rng){
        memset(owner,0,sizeof(owner));
        uniform_int_distribution<int> d(1,9);
        for(int r=0;r<R;r++)for(int c=0;c<C;c++){val[r][c]=d(rng);alive[r][c]=true;}
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
            return a.area>b.area;
        });
        return res;
    }
    void applyMove(const Move&m,int player){
        if(m.isPass())return;
        for(int r=m.r1;r<=m.r2;r++)for(int c=m.c1;c<=m.c2;c++){alive[r][c]=false;owner[r][c]=player;}
    }
    static long long topGainSum(const vector<Move>&v,int k){ long long s=0; for(int i=0;i<min(k,(int)v.size());i++)s+=v[i].gain; return s; }
    static long long topStealSum(const vector<Move>&v,int k){ long long s=0; for(int i=0;i<min(k,(int)v.size());i++)s+=v[i].stolen; return s; }

    // ===== bot_strong 의 손튜닝 정적 평가 (정답 라벨용) =====
    long long evaluate(int player) const {
        int e=3-player;
        vector<Move> mine=getCandidates(player), they=getCandidates(e);
        int diff=diffScore(player);
        long long myGain1=mine.empty()?0:mine[0].gain, opGain1=they.empty()?0:they[0].gain;
        long long myGain3=topGainSum(mine,3), opGain3=topGainSum(they,3);
        long long mySteal3=topStealSum(mine,3), opSteal3=topStealSum(they,3);
        long long s=0;
        s+=120000LL*diff;
        s+= 14000LL*myGain1;
        s+=  7000LL*myGain3;
        s+=  9000LL*mySteal3;
        s-= 23000LL*opGain1;
        s-= 15000LL*opGain3;
        s-= 16000LL*opSteal3;
        s+= 100LL*((int)mine.size()-(int)they.size());
        return s;
    }

    long long terminalScore(int player) const {
        int d=diffScore(player);
        if(d>0) return 1000000000LL+1000000LL*d;
        if(d==0) return 0;
        return -1000000000LL+1000000LL*d;
    }
    Move passMove() const { return Move{}; }
    // 라벨용 경량 beam 탐색 (상위 beam 후보를 orderScore 순으로)
    vector<Move> pickBeam(int player,int beam) const {
        vector<Move> all=getCandidates(player);
        if((int)all.size()>beam) all.resize(beam);
        return all;
    }
    long long negamax(int player,int depth,bool prevPass,int beam,long long alpha,long long beta) const {
        if(depth<=0) return evaluate(player);
        vector<Move> moves=pickBeam(player,beam);
        if(occupiedCount()>0)moves.push_back(passMove());
        if(moves.empty()) return evaluate(player);
        long long best=LLONG_MIN/4; int nb=max(4,beam-2);
        for(const Move&m:moves){
            long long sc;
            if(m.isPass()){
                if(prevPass)sc=terminalScore(player);
                else{Game nx=*this;nx.applyMove(m,player);sc=-nx.negamax(3-player,depth-1,true,nb,-beta,-alpha);}
            } else {Game nx=*this;nx.applyMove(m,player);sc=-nx.negamax(3-player,depth-1,false,nb,-beta,-alpha);}
            best=max(best,sc); alpha=max(alpha,sc);
            if(alpha>=beta)break;
        }
        return best;
    }
    // 라벨값: LABEL_DEPTH>0 이면 깊은 탐색값, 아니면 정적 평가
    long long labelValue(int player) const {
        if(LABEL_DEPTH>0) return negamax(player,LABEL_DEPTH,false,LABEL_BEAM,LLONG_MIN/4,LLONG_MAX/4);
        return evaluate(player);
    }

    // ===== nnue_bot.cpp 와 동일한 feature (player 관점) ===== 병합 스캔
    void features(int player, float* f) const {
        int e=3-player; int idx=0; int aliveCnt=0, meC=0, opC=0;
        int psV[R+1][C+1]{}, psA[R+1][C+1]{}, pMe[R+1][C+1]{}, pOp[R+1][C+1]{};
        for(int r=0;r<R;r++)for(int c=0;c<C;c++){
            f[idx++] = alive[r][c] ? (val[r][c]/9.0f) : 0.0f;
            f[idx++] = (owner[r][c]==player) ? 1.0f : 0.0f;
            f[idx++] = (owner[r][c]==e)      ? 1.0f : 0.0f;
            if(alive[r][c]) aliveCnt++;
            if(owner[r][c]==player) meC++; else if(owner[r][c]==e) opC++;
            int av=alive[r][c]?1:0, v=av?val[r][c]:0;
            int mm=(owner[r][c]==player)?1:0, oo=(owner[r][c]==e)?1:0;
            psV[r+1][c+1]=psV[r][c+1]+psV[r+1][c]-psV[r][c]+v;
            psA[r+1][c+1]=psA[r][c+1]+psA[r+1][c]-psA[r][c]+av;
            pMe[r+1][c+1]=pMe[r][c+1]+pMe[r+1][c]-pMe[r][c]+mm;
            pOp[r+1][c+1]=pOp[r][c+1]+pOp[r+1][c]-pOp[r][c]+oo;
        }
        auto Q=[&](int p[R+1][C+1],int r1,int c1,int r2,int c2){
            return p[r2+1][c2+1]-p[r1][c2+1]-p[r2+1][c1]+p[r1][c1];};
        auto push3=[](int&a,int&b,int&c,int x){
            if(x>a){c=b;b=a;a=x;} else if(x>b){c=b;b=x;} else if(x>c)c=x;};
        int cnt=0, mg1=0,mg2=0,mg3=0, og1=0,og2=0,og3=0, ms1=0,ms2=0,ms3=0, os1=0,os2=0,os3=0;
        for(int r1=0;r1<R;r1++)for(int r2=r1;r2<R;r2++)for(int c1=0;c1<C;c1++)for(int c2=c1;c2<C;c2++){
            if(Q(psV,r1,c1,r2,c2)!=10)continue;
            if(Q(psA,r1,c1,r1,c2)==0||Q(psA,r2,c1,r2,c2)==0||
               Q(psA,r1,c1,r2,c1)==0||Q(psA,r1,c2,r2,c2)==0)continue;
            int n=Q(psA,r1,c1,r2,c2), a=Q(pMe,r1,c1,r2,c2), b=Q(pOp,r1,c1,r2,c2);
            cnt++;
            push3(mg1,mg2,mg3,n+2*b); push3(og1,og2,og3,n+2*a);
            push3(ms1,ms2,ms3,b);     push3(os1,os2,os3,a);
        }
        f[idx++]=(meC-opC)/50.0f;
        f[idx++]=(meC+opC)/170.0f;
        f[idx++]=aliveCnt/170.0f;
        f[idx++]=cnt/60.0f;
        f[idx++]=mg1/20.0f;
        f[idx++]=og1/20.0f;
        f[idx++]=(mg1+mg2+mg3)/40.0f;
        f[idx++]=(og1+og2+og3)/40.0f;
        f[idx++]=(ms1+ms2+ms3)/20.0f;
        f[idx++]=(os1+os2+os3)/20.0f;
    }
};

int main(int argc,char**argv){
    if(argc<4){ fprintf(stderr,"usage: distill_gen out num_games seed [epsilon] [label_depth] [label_beam]\n"); return 1; }
    string out=argv[1];
    int numGames=atoi(argv[2]);
    unsigned seed=(unsigned)strtoul(argv[3],nullptr,10);
    double eps=(argc>4)?atof(argv[4]):0.3;
    if(argc>5)LABEL_DEPTH=atoi(argv[5]);
    if(argc>6)LABEL_BEAM=atoi(argv[6]);

    mt19937 rng(seed);
    uniform_real_distribution<double> uni(0.0,1.0);

    FILE* fo=fopen(out.c_str(),"w");
    if(!fo){ fprintf(stderr,"cannot open %s\n",out.c_str()); return 1; }

    float f[IN];
    string line; line.reserve(IN*8);
    char buf[32];

    for(int g=0; g<numGames; g++){
        Game game; game.init(rng);
        int turn=1, passes=0, capTurns=4*R*C;
        while(passes<2 && capTurns-->0){
            vector<Move> cands=game.getCandidates(turn);
            if(cands.empty()){ passes++; turn=3-turn; continue; }
            // 위치 기록
            game.features(turn,f);
            long long lv=game.labelValue(turn);                // 정적 또는 깊은 탐색값
            if(lv> 15000000LL)lv= 15000000LL;                  // 종료국면 폭주 클램프
            if(lv<-15000000LL)lv=-15000000LL;
            double target=(double)lv/SCALE;                    // 선형 타깃(포화 없음)
            line.clear();
            for(int i=0;i<IN;i++){ snprintf(buf,sizeof(buf),"%.5f ",f[i]); line+=buf; }
            snprintf(buf,sizeof(buf),"%.6f\n",target); line+=buf;
            fputs(line.c_str(),fo);
            // 수 선택: epsilon 무작위 / 아니면 탐욕(orderScore 최상)
            const Move& mv = (uni(rng)<eps) ? cands[rng()%cands.size()] : cands[0];
            game.applyMove(mv,turn);
            passes=0; turn=3-turn;
        }
    }
    fclose(fo);
    return 0;
}
