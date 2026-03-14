#include <bits/stdc++.h>
using namespace std;

// ===== 공통 도메인 =====
enum DiceRule { ONE, TWO, THREE, FOUR, FIVE, SIX, CHOICE, FOUR_OF_A_KIND, FULL_HOUSE, SMALL_STRAIGHT, LARGE_STRAIGHT, YACHT };
static DiceRule fromString(const string& s){ if(s=="ONE")return ONE; if(s=="TWO")return TWO; if(s=="THREE")return THREE; if(s=="FOUR")return FOUR; if(s=="FIVE")return FIVE; if(s=="SIX")return SIX; if(s=="CHOICE")return CHOICE; if(s=="FOUR_OF_A_KIND")return FOUR_OF_A_KIND; if(s=="FULL_HOUSE")return FULL_HOUSE; if(s=="SMALL_STRAIGHT")return SMALL_STRAIGHT; if(s=="LARGE_STRAIGHT")return LARGE_STRAIGHT; if(s=="YACHT")return YACHT; throw runtime_error("bad rule"); }
static int calcScore(DiceRule rule, const vector<int>& dice){ auto c=[&](int v){ return (int)count(dice.begin(),dice.end(),v); }; switch(rule){ case ONE:return c(1)*1000; case TWO:return c(2)*2000; case THREE:return c(3)*3000; case FOUR:return c(4)*4000; case FIVE:return c(5)*5000; case SIX:return c(6)*6000; case CHOICE:return accumulate(dice.begin(),dice.end(),0)*1000; case FOUR_OF_A_KIND:{ array<int,7> cc{}; for(int d:dice) if(1<=d&&d<=6) cc[d]++; bool ok=false; for(int i=1;i<=6;i++) if(cc[i]>=4){ok=true;break;} return ok? accumulate(dice.begin(),dice.end(),0)*1000:0;} case FULL_HOUSE:{ array<int,7> cc{}; for(int d:dice) if(1<=d&&d<=6) cc[d]++; bool pr=false,tr=false; for(int i=1;i<=6;i++){ int x=cc[i]; if(x==2||x==5) pr=true; if(x==3||x==5) tr=true;} return (pr&&tr)? accumulate(dice.begin(),dice.end(),0)*1000:0;} case SMALL_STRAIGHT:{ auto h=[&](int v){return c(v)>0;}; bool ok=(h(1)&&h(2)&&h(3)&&h(4))||(h(2)&&h(3)&&h(4)&&h(5))||(h(3)&&h(4)&&h(5)&&h(6)); return ok?15000:0;} case LARGE_STRAIGHT:{ auto h=[&](int v){return c(v)>0;}; bool ok=(h(1)&&h(2)&&h(3)&&h(4)&&h(5))||(h(2)&&h(3)&&h(4)&&h(5)&&h(6)); return ok?30000:0;} case YACHT:{ array<int,7>cc{}; for(int d:dice) if(1<=d&&d<=6) cc[d]++; for(int i=1;i<=6;i++) if(cc[i]==5) return 50000; return 0;} } return 0; }

struct Cand { DiceRule r; vector<int> use; int score; };
struct PutCtx { double upperNeedN_before, remainUpperRatio, remainLowerRatio; };

// ===== 온라인 러너(에이전트와 동일 파라미터 구조) =====
class OnlineLearner {
public:
    static constexpr int D_BID=32, D_AMT=34, D_PUT=25;
    static constexpr int K_GROUP=2, K_AMT=9;
    static const int AMOUNTS[K_AMT];

    vector<vector<double>> W_group; // 2 x D_BID
    vector<vector<double>> W_amt;   // 9 x D_AMT
    vector<double> w_put;           // D_PUT

    double lr=0.05, baseline_bid=0.0, baseline_put=0.0, gamma_b=0.99, gamma_p=0.99;

    OnlineLearner(): W_group(K_GROUP, vector<double>(D_BID,0.0)), W_amt(K_AMT, vector<double>(D_AMT,0.0)), w_put(D_PUT,0.0) {}

    // utils
    static double dot(const vector<double>& a, const vector<double>& b){ double s=0; for(size_t i=0;i<a.size();++i) s+=a[i]*b[i]; return s; }
    static vector<double> softmax(const vector<double>& z){ double m=*max_element(z.begin(),z.end()); vector<double> e(z.size()); double s=0; for(size_t i=0;i<z.size();++i){ e[i]=exp(z[i]-m); s+=e[i]; } for(double &v:e) v/= (s>0?s:1); return e; }

    struct DiceStats { int sum=0,cnt[7]{},maxCount=0,secondMax=0,distinct=0,longestRun=0,c1=0,c5=0,c6=0; int high()const{return c5+c6;} };
    static int longestRunFromCnt(const int c[7]){ int best=0,cur=0; for(int v=1;v<=6;++v){ if(c[v]>0){ cur++; best=max(best,cur);} else cur=0; } return best; }
    static DiceStats statsOf(const vector<int>& d){ DiceStats s; for(int x:d){ if(1<=x&&x<=6){ s.sum+=x; s.cnt[x]++; } } for(int v=1;v<=6;++v){ if(s.cnt[v]>0) s.distinct++; if(s.cnt[v]>s.maxCount){ s.secondMax=s.maxCount; s.maxCount=s.cnt[v]; } else s.secondMax=max(s.secondMax,s.cnt[v]); } s.longestRun=longestRunFromCnt(s.cnt); s.c1=s.cnt[1]; s.c5=s.cnt[5]; s.c6=s.cnt[6]; return s; }
    static bool hasSmallStraight(const DiceStats& s){ auto h=[&](int v){return s.cnt[v]>0;}; return (h(1)&&h(2)&&h(3)&&h(4))||(h(2)&&h(3)&&h(4)&&h(5))||(h(3)&&h(4)&&h(5)&&h(6)); }
    static bool hasLargeStraight(const DiceStats& s){ auto h=[&](int v){return s.cnt[v]>0;}; return (h(1)&&h(2)&&h(3)&&h(4)&&h(5))||(h(2)&&h(3)&&h(4)&&h(5)&&h(6)); }
    static bool hasFourKind(const DiceStats& s){ return s.maxCount>=4; }
    static bool hasFullHouse(const DiceStats& s){ return s.maxCount>=3 && s.secondMax>=2; }

    static vector<double> buildBidFeat(const vector<int>& A,const vector<int>& B,int lead){ DiceStats a=statsOf(A), b=statsOf(B); auto norm=[&](int x,int m){return (double)x/(double)m;}; double sa=a.sum/30.0,sb=b.sum/30.0,sd=(a.sum-b.sum)/30.0; double leadN=lead/100000.0, sign=(lead>0?1.0:(lead<0?-1.0:0.0)); vector<double> f; f.reserve(D_BID);
        f.insert(f.end(), {1.0,sa,sb,sd,leadN,sign});
        f.push_back(norm(a.longestRun,5)); f.push_back(norm(b.longestRun,5)); f.push_back((a.longestRun-b.longestRun)/5.0);
        f.push_back(norm(a.maxCount,5));   f.push_back(norm(b.maxCount,5));   f.push_back((a.maxCount-b.maxCount)/5.0);
        f.push_back(norm(a.distinct,5));   f.push_back(norm(b.distinct,5));   f.push_back((a.distinct-b.distinct)/5.0);
        f.push_back(norm(a.c6,5)); f.push_back(norm(b.c6,5)); f.push_back((a.c6-b.c6)/5.0);
        f.push_back(norm(a.c5,5)); f.push_back(norm(b.c5,5)); f.push_back((a.c5-b.c5)/5.0);
        f.push_back(hasSmallStraight(a)?1.0:0.0); f.push_back(hasSmallStraight(b)?1.0:0.0);
        f.push_back(hasLargeStraight(a)?1.0:0.0); f.push_back(hasLargeStraight(b)?1.0:0.0);
        f.push_back(hasFourKind(a)?1.0:0.0);      f.push_back(hasFourKind(b)?1.0:0.0);
        f.push_back(hasFullHouse(a)?1.0:0.0);     f.push_back(hasFullHouse(b)?1.0:0.0);
        auto upperHigh=[&](const DiceStats&S){ return (5*S.c5+6*S.c6)/30.0; };
        f.push_back(upperHigh(a)); f.push_back(upperHigh(b)); f.push_back(upperHigh(a)-upperHigh(b));
        return f; }
    static vector<double> buildAmtFeat(const vector<int>& A,const vector<int>& B,int lead){ auto f=buildBidFeat(A,B,lead); int sa=0,sb=0; for(int x:A)sa+=x; for(int x:B)sb+=x; f.push_back(std::abs(lead)/100000.0); f.push_back(std::abs(sa-sb)/30.0); return f; }

    static vector<double> buildPutFeat(const Cand& c, const PutCtx& ctx){ auto S=statsOf(c.use); auto norm=[&](int x,int m){return (double)x/(double)m;}; double scoreN=c.score/50000.0; double sumN=S.sum/30.0; double maxN=norm(S.maxCount,5); double distN=norm(S.distinct,5); double runN=norm(S.longestRun,5); double c6N=norm(S.c6,5), c1N=norm(S.c1,5); double upperGainN=(ONE<=c.r&&c.r<=SIX)?(c.score/63000.0):0.0; double highFacesN=norm(S.high(),5); vector<double> f; f.reserve(D_PUT);
        f.push_back(1.0); f.push_back(scoreN); for(int i=0;i<12;i++) f.push_back(0.0); f[2+(int)c.r]=1.0;
        f.push_back(sumN); f.push_back(maxN); f.push_back(distN); f.push_back(runN); f.push_back(c6N); f.push_back(c1N);
        f.push_back(ctx.upperNeedN_before); f.push_back(upperGainN); f.push_back(ctx.remainUpperRatio); f.push_back(ctx.remainLowerRatio); f.push_back(highFacesN);
        return f; }

    // 저장/로드
    string saveText() const{ ostringstream os; os.setf(ios::fixed); os<<setprecision(8); os<<"RLTXT v1\n"; os<<"LR "<<lr<<"\n"; os<<"BASE_BID "<<baseline_bid<<"\n"; os<<"BASE_PUT "<<baseline_put<<"\n"; os<<"W_GROUP "<<K_GROUP<<" "<<D_BID<<"\n"; for(int i=0;i<K_GROUP;++i){ for(int j=0;j<D_BID;++j){ if(j) os<<' '; os<<W_group[i][j]; } os<<"\n"; } os<<"W_AMT "<<K_AMT<<" "<<D_AMT<<"\n"; for(int i=0;i<K_AMT;++i){ for(int j=0;j<D_AMT;++j){ if(j) os<<' '; os<<W_amt[i][j]; } os<<"\n"; } os<<"W_PUT "<<D_PUT<<"\n"; for(int j=0;j<D_PUT;++j){ if(j) os<<' '; os<<w_put[j]; } os<<"\nEND\n"; return os.str(); }
    bool loadFromStream(istream& in){ string tag,ver; if(!(in>>tag>>ver)) return false; if(tag!="RLTXT"||ver!="v1") return false; string key; while(in>>key){ if(key=="END") break; if(key=="LR") in>>lr; else if(key=="BASE_BID") in>>baseline_bid; else if(key=="BASE_PUT") in>>baseline_put; else if(key=="W_GROUP"){ int R,C; in>>R>>C; if(R!=K_GROUP||C!=D_BID) return false; for(int i=0;i<R;++i) for(int j=0;j<C;++j) in>>W_group[i][j]; } else if(key=="W_AMT"){ int R,C; in>>R>>C; if(R!=K_AMT||C!=D_AMT) return false; for(int i=0;i<R;++i) for(int j=0;j<C;++j) in>>W_amt[i][j]; } else if(key=="W_PUT"){ int C; in>>C; if(C!=D_PUT) return false; for(int j=0;j<C;++j) in>>w_put[j]; } else return false; if(!in) return false; } return true; }
    bool loadFromFile(const string& path){ ifstream fin(path, ios::binary); if(!fin) return false; return loadFromStream(fin); }

    // 오프라인 업데이트
    void updateBidFromContext(const vector<int>& A,const vector<int>& B,int lead, int chosenGroupIdx, int chosenAmtIdx, bool ok){
        auto xg = buildBidFeat(A,B,lead); auto xa = buildAmtFeat(A,B,lead);
        vector<double> lg(K_GROUP,0.0); for(int g=0;g<K_GROUP;++g) lg[g]=dot(W_group[g], xg); auto pG=softmax(lg);
        double r = (ok? -AMOUNTS[chosenAmtIdx] : +AMOUNTS[chosenAmtIdx]) / 100000.0;
        for(int g=0; g<K_GROUP; ++g){ double coeff=((g==chosenGroupIdx)?1.0:0.0)-pG[g]; for(int d=0; d<D_BID; ++d) W_group[g][d]+= lr*(r-baseline_bid)*coeff*xg[d]; }
        vector<double> la(K_AMT,0.0); for(int k=0;k<K_AMT;++k) la[k]=dot(W_amt[k], xa); auto pA=softmax(la);
        for(int k=0;k<K_AMT;++k){ double coeff=((k==chosenAmtIdx)?1.0:0.0)-pA[k]; for(int d=0; d<D_AMT; ++d) W_amt[k][d]+= lr*(r-baseline_bid)*coeff*xa[d]; }
        baseline_bid = gamma_b*baseline_bid + (1.0-gamma_b)*r;
    }
    void updatePutFromCandidates(const vector<Cand>& cs, const PutCtx& ctx, int selIdx, int rewardScore){
        vector<vector<double>> feats; feats.reserve(cs.size());
        for(auto &c: cs) feats.push_back(buildPutFeat(c, ctx));
        vector<double> logit(cs.size(),0.0); for(size_t j=0;j<cs.size();++j) logit[j]=dot(w_put, feats[j]); auto p=softmax(logit);
        double r = rewardScore/50000.0; vector<double> grad(w_put.size(),0.0);
        for(int d=0; d<(int)w_put.size(); ++d){ double expect=0.0; for(size_t j=0;j<cs.size();++j) expect += p[j]*feats[j][d]; grad[d]= feats[selIdx][d]-expect; }
        for(int d=0; d<(int)w_put.size(); ++d) w_put[d] += lr*(r-baseline_put)*grad[d];
        baseline_put = gamma_p*baseline_put + (1.0-gamma_p)*r;
    }
    string dumpJSON() const{ ostringstream os; os.setf(ios::fixed); os<<setprecision(5); os<<"{\"lr\":"<<lr<<",\"baseline_bid\":"<<baseline_bid<<",\"baseline_put\":"<<baseline_put<<"}"; return os.str(); }
};
const int OnlineLearner::AMOUNTS[OnlineLearner::K_AMT]={0,1000,2000,5000,10000,20000,50000,80000,100000};

// ===== 간단 시뮬 상태/유틸 (학습 데이터 줄 처리용) =====
struct SimState {
    vector<int> dice; array<int,7> cnt{}; vector<int> ruleScore; int bidScore;
    SimState(): ruleScore(12,-1), bidScore(0) {}
    int getTotalScore() const {
        int basic=0, comb=0, bonus=0;
        for(int i=0;i<6;i++) if(ruleScore[i]!=-1) basic += ruleScore[i];
        if(basic >= 63000) bonus += 35000;
        for(int i=6;i<12;i++) if(ruleScore[i]!=-1) comb += ruleScore[i];
        return basic + bonus + comb + bidScore;
    }
    void addDice(const vector<int>& nd){ dice.reserve(dice.size()+nd.size()); for(int d:nd){ dice.push_back(d); if(1<=d&&d<=6) cnt[d]++; } }
    void bid(bool ok,int amount){ if(ok) bidScore -= amount; else bidScore += amount; }
};
static void useCandidate(SimState& st, const Cand& c){ for(int d: c.use){ if(1<=d && d<=6) st.cnt[d]--; } st.ruleScore[(int)c.r] = calcScore(c.r, c.use); }
static vector<Cand> buildCandidates(const SimState& st){
    const auto &C = st.cnt; vector<Cand> cs; cs.reserve(20);
    auto build_multi=[&](int v,int k){ vector<int> out; out.reserve(k); for(int i=0;i<k;i++) out.push_back(v); return out; };
    auto fillToFive=[&](vector<int> base){ array<int,7> tmp=C; for(int d:base){ if(!(1<=d&&d<=6) || tmp[d]==0) return vector<int>{}; tmp[d]--; } for(int v=6; v>=1 && (int)base.size()<5; --v){ while(tmp[v]>0 && (int)base.size()<5){ base.push_back(v); tmp[v]--; } } if(base.size()!=5) return vector<int>{}; return base; };
    auto pickTop5=[&](){ vector<int> out; out.reserve(5); for(int v=6; v>=1 && (int)out.size()<5; --v){ int t=min(C[v], 5-(int)out.size()); for(int i=0;i<t;i++) out.push_back(v);} return out; };
    auto selectYacht=[&](){ for(int v=1;v<=6;++v) if(C[v]>=5) return build_multi(v,5); return vector<int>{}; };
    auto selectLarge=[&](){ int a[5]={1,2,3,4,5}, b[5]={2,3,4,5,6}; auto ok=[&](int *s){ for(int i=0;i<5;i++) if(C[s[i]]==0) return false; return true; }; if(ok(a)) return vector<int>{1,2,3,4,5}; if(ok(b)) return vector<int>{2,3,4,5,6}; return vector<int>{}; };
    auto selectSmall=[&](){ int a[4]={1,2,3,4}, b[4]={2,3,4,5}, c[4]={3,4,5,6}; auto ok=[&](int *s){ for(int i=0;i<4;i++) if(C[s[i]]==0) return false; return true; }; if(ok(a)) return vector<int>{1,2,3,4}; if(ok(b)) return vector<int>{2,3,4,5}; if(ok(c)) return vector<int>{3,4,5,6}; return vector<int>{}; };
    auto selectFour=[&](){ for(int v=6; v>=1; --v) if(C[v]>=4){ int extra=-1; for(int x=6;x>=1;--x) if(x!=v && C[x]>0){ extra=x; break; } if(extra==-1) extra=v; return vector<int>{v,v,v,v,extra}; } return vector<int>{}; };
    auto selectFull=[&](){ for(int v=1;v<=6;++v) if(C[v]==5) return build_multi(v,5); int tri=-1,pai=-1; for(int v=6;v>=1;--v) if(C[v]>=3){ tri=v; break; } for(int v=6;v>=1;--v) if(v!=tri && C[v]>=2){ pai=v; break; } if(tri!=-1&&pai!=-1) return vector<int>{tri,tri,tri,pai,pai}; return vector<int>{}; };
    auto selectChoice=[&](){ return pickTop5(); };
    auto selectUpper=[&](int face){ int use=min(5, C[face]); return vector<int>(use, face); };
    auto usableRule=[&](DiceRule r){ return st.ruleScore[(int)r]==-1; };
    auto pushIf=[&](DiceRule r, vector<int> d){ if(!usableRule(r)) return; if(d.empty() && !(ONE<=r&&r<=SIX)) return; d=fillToFive(d); if(d.empty()) return; int sc=calcScore(r,d); cs.push_back({r,d,sc}); };
    pushIf(YACHT,selectYacht()); pushIf(LARGE_STRAIGHT,selectLarge()); pushIf(FOUR_OF_A_KIND,selectFour()); pushIf(FULL_HOUSE,selectFull()); pushIf(CHOICE,selectChoice()); pushIf(SMALL_STRAIGHT,selectSmall()); for(int face=6; face>=1; --face) pushIf((DiceRule)(face-1), selectUpper(face));
    return cs;
}
static PutCtx makeCtx(const SimState& st){ int upperSum=0; for(int i=0;i<6;i++) if(st.ruleScore[i]!=-1) upperSum+=st.ruleScore[i]; int remU=0, remL=0; for(int i=0;i<6;i++) if(st.ruleScore[i]==-1) remU++; for(int i=6;i<12;i++) if(st.ruleScore[i]==-1) remL++; double upperNeed = max(0,63000-upperSum)/63000.0; return PutCtx{upperNeed, remU/6.0, remL/6.0}; }
static bool is5digits(const string& s){ if(s.size()!=5) return false; for(char c: s) if(c<'0'||c>'9') return false; return true; }

// ===== 기존 명령 모드 + 간단 라인 모드 병행 =====
static int mapAmountToIdx(int amt){ int best=0; int bestDiff=INT_MAX; for(int i=0;i<OnlineLearner::K_AMT;i++){ int d=abs(amt-OnlineLearner::AMOUNTS[i]); if(d<bestDiff){bestDiff=d; best=i;} } return best; }

int main(){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    OnlineLearner rl;
    rl.loadFromFile("data.bin");

    // 간단 라인 모드용 상태
    SimState my, opp;

    string line; cout.setf(ios::fixed); cout<<setprecision(6);
    while (true){
        if(!getline(cin,line)) break;
        if(line.empty()) continue;
        if(line[0]=='#') continue;

        istringstream iss(line);
        string cmd; iss >> cmd;

        // ---- 간단 라인 모드: "ABCDE FGHIJ T" ----
        if (is5digits(cmd)) {
            string b; int tie = 0; iss >> b >> tie;
            if (!is5digits(b)) { cerr<<"Bad simple line\n"; continue; }

            vector<int> A, B;
            for(char c:cmd) A.push_back(c-'0');
            for(char c:b)   B.push_back(c-'0');

            int lead = my.getTotalScore() - opp.getTotalScore();

            // 정책 기반 BID 추정(학습 X): argmax로 결정
            auto xg = OnlineLearner::buildBidFeat(A,B,lead);
            auto xa = OnlineLearner::buildAmtFeat(A,B,lead);
            vector<double> lg(OnlineLearner::K_GROUP,0.0); for(int g=0;g<OnlineLearner::K_GROUP;++g) lg[g]=OnlineLearner::dot(rl.W_group[g], xg);
            int gIdx = (lg[1] > lg[0] ? 1 : 0); char grp = (gIdx==0?'A':'B');
            vector<double> la(OnlineLearner::K_AMT,0.0); for(int k=0;k<OnlineLearner::K_AMT;++k) la[k]=OnlineLearner::dot(rl.W_amt[k], xa);
            int aIdx=0; for(int k=1;k<OnlineLearner::K_AMT;++k) if(la[k]>la[aIdx]) aIdx=k; int amount = OnlineLearner::AMOUNTS[aIdx];

            // ★ BID 학습 금지: 동률만 가정하고 tie로 결과만 반영
            bool myOk = (tie==0);
            char myGroup = myOk ? grp : (grp=='A'?'B':'A');
            if (myGroup=='A'){ my.addDice(A); opp.addDice(B); } else { my.addDice(B); opp.addDice(A); }
            my.bid(myOk, amount); opp.bid(!myOk, amount);

            // PUT: 후보 생성→선택(argmax)→보상으로 학습
            auto cs = buildCandidates(my);
            if(!cs.empty()){
                auto ctx = makeCtx(my);
                vector<double> logit(cs.size(),0.0);
                for(size_t i=0;i<cs.size();++i){ auto f = OnlineLearner::buildPutFeat(cs[i], ctx); logit[i]=OnlineLearner::dot(rl.w_put, f); }
                int sel=0; for(size_t i=1;i<cs.size();++i) if(logit[i]>logit[sel]) sel=(int)i;
                int reward = cs[sel].score;
                rl.updatePutFromCandidates(cs, ctx, sel, reward);
                useCandidate(my, cs[sel]);
            }
            // 상대는 점수만 맞추기 위해 탐욕 배치(학습X)
            auto csOpp = buildCandidates(opp);
            if(!csOpp.empty()){ int selO=0; for(size_t i=1;i<csOpp.size();++i) if(csOpp[i].score>csOpp[selO].score) selO=(int)i; useCandidate(opp, csOpp[selO]); }

            continue;
        }

        // ---- 기존 명령 모드 ----
        if(cmd=="LOAD"){ if(rl.loadFromFile("data.bin")) cout<<"OK\n"; else cout<<"NOFILE\n"; }
        else if(cmd=="SAVE"){ ofstream fout("data.bin", ios::binary); string txt=rl.saveText(); fout.write(txt.data(), (long long)txt.size()); cout<<"OK\n"; }
        else if(cmd=="LR"){ double x; iss>>x; rl.lr=x; cout<<"OK\n"; }
        else if(cmd=="DUMP"){ cout<<"MODEL "<<rl.dumpJSON()<<"\n"; }
        else if(cmd=="DUMP_TXT"){ cout<<"MODEL_TXT_BEGIN\n"<<rl.saveText()<<"MODEL_TXT_END\n"; }
        else if(cmd=="BID"){ string tok; string As, Bs; int lead=0; char choice='A'; int amount=0; int ok=0; int tie=0; while(iss>>tok){ auto pos=tok.find('='); string k=tok.substr(0,pos), v=(pos==string::npos?string(""):tok.substr(pos+1)); if(k=="A") As=v; else if(k=="B") Bs=v; else if(k=="LEAD") lead=stoi(v); else if(k=="CHOICE") choice=v[0]; else if(k=="AMOUNT") amount=stoi(v); else if(k=="OK") ok=stoi(v); else if(k=="TIE") tie=stoi(v); }
            vector<int> A,B; for(char c:As) if('0'<=c&&c<='9') A.push_back(c-'0'); for(char c:Bs) if('0'<=c&&c<='9') B.push_back(c-'0'); int gIdx=(choice=='A'?0:1); int aIdx=mapAmountToIdx(amount);
            if(!tie) rl.updateBidFromContext(A,B,lead,gIdx,aIdx,(bool)ok); // 동률 라운드 학습 제외
            cout<<"OK\n";
        }
        else if(cmd=="PUT"){ string sub; iss>>sub; if(sub=="BEGIN"){
                string l2; if(!getline(cin,l2)) break; istringstream is2(l2); string kw; is2>>kw; if(kw!="CTX"){ cerr<<"Expected CTX line\n"; return 1; }
                double upperNeed=0, RU=0, RLv=0; string t; while(is2>>t){ auto p=t.find('='); string k=t.substr(0,p); string v=t.substr(p+1); if(k=="UPPERNEED") upperNeed=stod(v); else if(k=="RU") RU=stod(v); else if(k=="RL") RLv=stod(v); }
                string l3; if(!getline(cin,l3)) break; istringstream is3(l3); int N; string Nkw; is3>>Nkw>>N; if(Nkw!="N"){ cerr<<"Expected N line\n"; return 1; }
                vector<Cand> cs; cs.reserve(N);
                for(int i=0;i<N;i++){ string li; if(!getline(cin,li)) break; if(li.empty()){ i--; continue; } istringstream isc(li); char Cc; isc>>Cc; if(Cc!='C'){ cerr<<"Expected candidate line 'C'\n"; return 1; } string rule, ds; isc>>rule>>ds; vector<int> dice; for(char c:ds) if('0'<=c&&c<='9') dice.push_back(c-'0'); DiceRule r=fromString(rule); int sc = calcScore(r,dice); cs.push_back(Cand{r,dice,sc}); }
                string l4; if(!getline(cin,l4)) break; istringstream is4(l4); string ssel; int sel=0; string srew; int rew=0; is4>>ssel>>sel>>srew>>rew; if(ssel!="SEL"||srew!="REWARD"){ cerr<<"Expected 'SEL <i> REWARD <score>'\n"; return 1; }
                rl.updatePutFromCandidates(cs, PutCtx{upperNeed,RU,RLv}, sel, rew); cout<<"OK\n";
            } else if(sub=="END") { cout<<"OK\n"; } else { cerr<<"Unknown PUT subcmd\n"; return 1; }
        } else { cerr<<"Unknown cmd: "<<cmd<<"\n"; }
    }

    // 종료 시 자동 저장
    ofstream fout("data.bin", ios::binary); string txt=rl.saveText(); fout.write(txt.data(), (long long)txt.size());
    return 0;
}
