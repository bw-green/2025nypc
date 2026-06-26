#include <bits/stdc++.h>
using namespace std;

// ===================== 도메인 정의 =====================
enum DiceRule { ONE, TWO, THREE, FOUR, FIVE, SIX,
    CHOICE, FOUR_OF_A_KIND, FULL_HOUSE, SMALL_STRAIGHT, LARGE_STRAIGHT, YACHT };

struct Bid { char group; int amount; };
struct DicePut { DiceRule rule; vector<int> dice; };

struct GameState {
    vector<int> dice;        // 출력용
    array<int,7> cnt{};      // 1..6 보유 개수
    vector<int> ruleScore;   // 규칙별 점수(-1: 미사용)
    int bidScore;            // 입찰 누적 점수

    GameState(): ruleScore(12, -1), bidScore(0) {}

    int getTotalScore() const {
        int basic=0, comb=0, bonus=0;
        for(int i=0;i<6;i++) if(ruleScore[i]!=-1) basic += ruleScore[i];
        if(basic >= 63000) bonus += 35000;
        for(int i=6;i<12;i++) if(ruleScore[i]!=-1) comb += ruleScore[i];
        return basic + bonus + comb + bidScore;
    }
    void bid(bool ok, int amount){ if(ok) bidScore -= amount; else bidScore += amount; }
    void addDice(const vector<int>& nd){ dice.reserve(dice.size()+nd.size()); for(int d:nd){ dice.push_back(d); if(1<=d&&d<=6) cnt[d]++; } }
    void useDice(const DicePut& put){
        assert(ruleScore[put.rule]==-1);
        for(int d: put.dice){ assert(1<=d&&d<=6 && cnt[d]>0); cnt[d]--; }
        ruleScore[put.rule] = calculateScore(put.rule, put.dice);
    }

    static int calculateScore(DiceRule rule, const vector<int>& dice){
        auto c=[&](int v){ return (int)count(dice.begin(), dice.end(), v); };
        switch(rule){
            case ONE:   return c(1)*1000;
            case TWO:   return c(2)*2000;
            case THREE: return c(3)*3000;
            case FOUR:  return c(4)*4000;
            case FIVE:  return c(5)*5000;
            case SIX:   return c(6)*6000;
            case CHOICE: return accumulate(dice.begin(), dice.end(), 0)*1000;
            case FOUR_OF_A_KIND: {
                array<int,7> cc{}; for(int d:dice) if(1<=d&&d<=6) cc[d]++;
                bool ok=false; for(int i=1;i<=6;i++) if(cc[i]>=4) {ok=true; break;}
                return ok? accumulate(dice.begin(), dice.end(), 0)*1000 : 0;
            }
            case FULL_HOUSE: {
                array<int,7> cc{}; for(int d:dice) if(1<=d&&d<=6) cc[d]++;
                bool pr=false, tr=false; for(int i=1;i<=6;i++){ int x=cc[i]; if(x==2||x==5) pr=true; if(x==3||x==5) tr=true; }
                return (pr&&tr)? accumulate(dice.begin(), dice.end(), 0)*1000 : 0;
            }
            case SMALL_STRAIGHT: {
                auto h=[&](int v){ return c(v)>0; };
                bool ok = (h(1)&&h(2)&&h(3)&&h(4)) || (h(2)&&h(3)&&h(4)&&h(5)) || (h(3)&&h(4)&&h(5)&&h(6));
                return ok? 15000:0;
            }
            case LARGE_STRAIGHT: {
                auto h=[&](int v){ return c(v)>0; };
                bool ok = (h(1)&&h(2)&&h(3)&&h(4)&&h(5)) || (h(2)&&h(3)&&h(4)&&h(5)&&h(6));
                return ok? 30000:0;
            }
            case YACHT: {
                array<int,7> cc{}; for(int d:dice) if(1<=d&&d<=6) cc[d]++;
                for(int i=1;i<=6;i++) if(cc[i]==5) return 50000; return 0;
            }
        }
        assert(false); return 0;
    }
};

static string toString(DiceRule rule){
    switch(rule){
        case ONE: return "ONE"; case TWO: return "TWO"; case THREE: return "THREE"; case FOUR: return "FOUR"; case FIVE: return "FIVE"; case SIX: return "SIX";
        case CHOICE: return "CHOICE"; case FOUR_OF_A_KIND: return "FOUR_OF_A_KIND"; case FULL_HOUSE: return "FULL_HOUSE"; case SMALL_STRAIGHT: return "SMALL_STRAIGHT";
        case LARGE_STRAIGHT: return "LARGE_STRAIGHT"; case YACHT: return "YACHT";
    } return "";
}
static DiceRule fromString(const string& s){
    if(s=="ONE")return ONE; if(s=="TWO")return TWO; if(s=="THREE")return THREE; if(s=="FOUR")return FOUR; if(s=="FIVE")return FIVE; if(s=="SIX")return SIX;
    if(s=="CHOICE")return CHOICE; if(s=="FOUR_OF_A_KIND")return FOUR_OF_A_KIND; if(s=="FULL_HOUSE")return FULL_HOUSE;
    if(s=="SMALL_STRAIGHT")return SMALL_STRAIGHT; if(s=="LARGE_STRAIGHT")return LARGE_STRAIGHT; if(s=="YACHT")return YACHT; assert(false); return ONE;
}

// ===================== RL 학습기 =====================
struct Cand { DiceRule r; vector<int> use; int score; };
struct PutCtx { double upperNeedN_before, remainUpperRatio, remainLowerRatio; };

class OnlineLearner {
public:
    static constexpr int D_BID=32, D_AMT=34, D_PUT=25;
    static constexpr int K_GROUP=2, K_AMT=9;
    static const int AMOUNTS[K_AMT];

    vector<vector<double>> W_group; // 2 x D_BID
    vector<vector<double>> W_amt;   // 9 x D_AMT
    vector<double> w_put;           // D_PUT

    double lr=0.05; double baseline_bid=0.0, baseline_put=0.0; double gamma_b=0.99, gamma_p=0.99;
    vector<double> last_x_group, last_x_amt; int last_group_idx=-1, last_amt_idx=-1;
    vector<vector<double>> last_put_feats; int last_put_sel=-1;
    bool verbose=false; mt19937 rng;

    OnlineLearner(): W_group(K_GROUP, vector<double>(D_BID,0.0)), W_amt(K_AMT, vector<double>(D_AMT,0.0)), w_put(D_PUT,0.0), rng(random_device{}()) {}

    static double dot(const vector<double>& a, const vector<double>& b){ double s=0; for(size_t i=0;i<a.size();++i) s+=a[i]*b[i]; return s; }
    static vector<double> softmax(const vector<double>& z){ double m=*max_element(z.begin(),z.end()); vector<double> e(z.size()); double s=0; for(size_t i=0;i<z.size();++i){ e[i]=exp(z[i]-m); s+=e[i]; } for(double &v:e) v/= (s>0?s:1); return e; }
    int sample(const vector<double>& p){ uniform_real_distribution<double>U(0.0,1.0); double r=U(rng),acc=0; for(int i=0;i<(int)p.size();++i){ acc+=p[i]; if(r<=acc) return i; } return (int)p.size()-1; }

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

    static vector<double> buildPutFeat(const Cand& c, const PutCtx& ctx){ DiceStats S=statsOf(c.use); auto norm=[&](int x,int m){return (double)x/(double)m;}; double scoreN=c.score/50000.0; double sumN=S.sum/30.0; double maxN=norm(S.maxCount,5); double distN=norm(S.distinct,5); double runN=norm(S.longestRun,5); double c6N=norm(S.c6,5), c1N=norm(S.c1,5); double upperGainN=(ONE<=c.r&&c.r<=SIX)?(c.score/63000.0):0.0; double highFacesN=norm(S.high(),5);
        vector<double> f; f.reserve(D_PUT);
        f.push_back(1.0); f.push_back(scoreN); for(int i=0;i<12;i++) f.push_back(0.0); f[2+(int)c.r]=1.0;
        f.push_back(sumN); f.push_back(maxN); f.push_back(distN); f.push_back(runN); f.push_back(c6N); f.push_back(c1N);
        f.push_back(ctx.upperNeedN_before); f.push_back(upperGainN); f.push_back(ctx.remainUpperRatio); f.push_back(ctx.remainLowerRatio); f.push_back(highFacesN);
        return f; }

    // --- 저장/로드 (텍스트 포맷 RLTXT v1) ---
    string saveText() const{
        ostringstream os; os.setf(ios::fixed); os<<setprecision(8);
        os<<"RLTXT v1\n";
        os<<"LR "<<lr<<"\n";
        os<<"BASE_BID "<<baseline_bid<<"\n";
        os<<"BASE_PUT "<<baseline_put<<"\n";
        os<<"W_GROUP "<<K_GROUP<<" "<<D_BID<<"\n";
        for(int i=0;i<K_GROUP;++i){ for(int j=0;j<D_BID;++j){ if(j) os<<' '; os<<W_group[i][j]; } os<<"\n"; }
        os<<"W_AMT "<<K_AMT<<" "<<D_AMT<<"\n";
        for(int i=0;i<K_AMT;++i){ for(int j=0;j<D_AMT;++j){ if(j) os<<' '; os<<W_amt[i][j]; } os<<"\n"; }
        os<<"W_PUT "<<D_PUT<<"\n"; for(int j=0;j<D_PUT;++j){ if(j) os<<' '; os<<w_put[j]; } os<<"\nEND\n"; return os.str(); }
    bool loadFromStream(istream& in){ string tag,ver; if(!(in>>tag>>ver)) return false; if(tag!="RLTXT"||ver!="v1") return false; string key; while(in>>key){ if(key=="END") break; if(key=="LR") in>>lr; else if(key=="BASE_BID") in>>baseline_bid; else if(key=="BASE_PUT") in>>baseline_put; else if(key=="W_GROUP"){ int R,C; in>>R>>C; if(R!=K_GROUP||C!=D_BID) return false; for(int i=0;i<R;++i) for(int j=0;j<C;++j) in>>W_group[i][j]; }
        else if(key=="W_AMT"){ int R,C; in>>R>>C; if(R!=K_AMT||C!=D_AMT) return false; for(int i=0;i<R;++i) for(int j=0;j<C;++j) in>>W_amt[i][j]; }
        else if(key=="W_PUT"){ int C; in>>C; if(C!=D_PUT) return false; for(int j=0;j<C;++j) in>>w_put[j]; } else return false; if(!in) return false; } return true; }
    bool loadFromFile(const string& path){ ifstream fin(path, ios::binary); if(!fin) return false; return loadFromStream(fin); }

    // --- 정책 ---
    Bid chooseBid(const vector<int>& A,const vector<int>& B,int lead){ last_x_group=buildBidFeat(A,B,lead); last_x_amt=buildAmtFeat(A,B,lead);
        vector<double> lg(K_GROUP,0.0); for(int g=0;g<K_GROUP;++g) lg[g]=dot(W_group[g], last_x_group); auto pG=softmax(lg); last_group_idx=sample(pG); char grp=(last_group_idx==0?'A':'B');
        vector<double> la(K_AMT,0.0); for(int k=0;k<K_AMT;++k) la[k]=dot(W_amt[k], last_x_amt); auto pA=softmax(la); last_amt_idx=sample(pA); int amt=AMOUNTS[last_amt_idx];
        if(verbose){ cerr.setf(ios::fixed); cerr<<setprecision(3)<<"[BID] pA="<<pG[0]<<","<<pG[1]<<" amtBucket0="<<pA[0]<<" amtChosen="<<amt<<"\n"; }
        return Bid{grp, amt}; }

    void observeBidOutcome(bool ok, int chosenAmount){ if(last_group_idx<0||last_amt_idx<0) return; double r=(ok? -chosenAmount : +chosenAmount)/100000.0;
        vector<double> lg(K_GROUP,0.0); for(int g=0;g<K_GROUP;++g) lg[g]=dot(W_group[g], last_x_group); auto pG=softmax(lg);
        for(int g=0;g<K_GROUP;++g){ double coeff=((g==last_group_idx)?1.0:0.0)-pG[g]; for(int d=0; d<D_BID; ++d) W_group[g][d]+= lr*(r-baseline_bid)*coeff*last_x_group[d]; }
        vector<double> la(K_AMT,0.0); for(int k=0;k<K_AMT;++k) la[k]=dot(W_amt[k], last_x_amt); auto pA=softmax(la);
        for(int k=0;k<K_AMT;++k){ double coeff=((k==last_amt_idx)?1.0:0.0)-pA[k]; for(int d=0; d<D_AMT; ++d) W_amt[k][d]+= lr*(r-baseline_bid)*coeff*last_x_amt[d]; }
        baseline_bid = gamma_b*baseline_bid + (1.0-gamma_b)*r; if(verbose){ cerr.setf(ios::fixed); cerr<<setprecision(3)<<"[BID-UPDATE] r="<<r<<" base="<<baseline_bid<<"\n"; }
        last_group_idx=last_amt_idx=-1; last_x_group.clear(); last_x_amt.clear(); }

    int choosePut(const vector<Cand>& cs, const vector<PutCtx>& ctxs){ last_put_feats.clear(); vector<double> logit(cs.size(),0.0); last_put_feats.reserve(cs.size()); for(size_t i=0;i<cs.size();++i){ last_put_feats.push_back(buildPutFeat(cs[i], ctxs[i])); logit[i]=dot(w_put,last_put_feats.back()); }
        auto p=softmax(logit); int sel=sample(p); last_put_sel=sel; if(verbose){ cerr.setf(ios::fixed); cerr<<setprecision(3)<<"[PUT] cand="<<cs.size()<<" selP="<<p[sel]<<" rule="<<cs[sel].r<<" score="<<cs[sel].score<<"\n"; } return sel; }
    void observePutReward(int score){ if(last_put_sel<0||last_put_feats.empty()) return; double r=score/50000.0; vector<double> logit(last_put_feats.size(),0.0);
        for(size_t j=0;j<last_put_feats.size();++j) logit[j]=dot(w_put,last_put_feats[j]); auto p=softmax(logit); vector<double> grad(w_put.size(),0.0);
        for(int d=0; d<(int)w_put.size(); ++d){ double expect=0.0; for(size_t j=0;j<last_put_feats.size();++j) expect += p[j]*last_put_feats[j][d]; grad[d]= last_put_feats[last_put_sel][d] - expect; }
        for(int d=0; d<(int)w_put.size(); ++d) w_put[d] += lr*(r-baseline_put)*grad[d]; baseline_put=gamma_p*baseline_put+(1.0-gamma_p)*r; if(verbose){ cerr.setf(ios::fixed); cerr<<setprecision(3)<<"[PUT-UPDATE] r="<<r<<" base="<<baseline_put<<"\n"; }
        last_put_sel=-1; last_put_feats.clear(); }

    // JSON 덤프(간단용)
    string dumpJSON() const{ ostringstream os; os.setf(ios::fixed); os<<setprecision(5); os<<"{\"lr\":"<<lr<<",\"baseline_bid\":"<<baseline_bid<<",\"baseline_put\":"<<baseline_put<<",\"w_put\":["; for(size_t i=0;i<w_put.size();++i){ if(i) os<<","; os<<w_put[i]; } os<<"]}"; return os.str(); }
};
const int OnlineLearner::AMOUNTS[OnlineLearner::K_AMT]={0,1000,2000,5000,10000,20000,50000,80000,100000};

// ===================== 게임 =====================
class Game{
public:
    GameState myState, oppState; OnlineLearner rl;

    Bid calculateBid(const vector<int>& diceA,const vector<int>& diceB){ int lead=myState.getTotalScore()-oppState.getTotalScore(); return rl.chooseBid(diceA,diceB,lead); }

    DicePut calculatePut(){ vector<int> usable; for(int r=0;r<12;++r) if(myState.ruleScore[r]==-1) usable.push_back(r); if(usable.empty()) return DicePut{ONE,{}};
        int total=0; for(int v=1;v<=6;++v) total+=myState.cnt[v]; if(total<5){ vector<int> any5; auto tmp=myState.cnt; for(int v=6; v>=1 && (int)any5.size()<5; --v){ while(tmp[v]>0 && (int)any5.size()<5){ any5.push_back(v); tmp[v]--; } } while((int)any5.size()<5) any5.push_back(1); return DicePut{ (DiceRule)usable[0], any5}; }
        const auto &C = myState.cnt;
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
        auto usableRule=[&](DiceRule r){ return myState.ruleScore[r]==-1; };

        vector<Cand> cs; cs.reserve(20);
        auto pushIf=[&](DiceRule r, vector<int> d){ if(!usableRule(r)) return; if(d.empty() && !(ONE<=r&&r<=SIX)) return; d=fillToFive(d); if(d.empty()) return; int sc=GameState::calculateScore(r,d); cs.push_back({r,d,sc}); };
        pushIf(YACHT,selectYacht()); pushIf(LARGE_STRAIGHT,selectLarge()); pushIf(FOUR_OF_A_KIND,selectFour()); pushIf(FULL_HOUSE,selectFull()); pushIf(CHOICE,selectChoice()); pushIf(SMALL_STRAIGHT,selectSmall()); for(int face=6; face>=1; --face) pushIf((DiceRule)(face-1), selectUpper(face));
        if(cs.empty()){ auto any5=pickTop5(); if((int)any5.size()<5){ array<int,7> tmp=C; for(int v=6; v>=1 && (int)any5.size()<5; --v){ while(tmp[v]>0 && (int)any5.size()<5){ any5.push_back(v); tmp[v]--; } } } return DicePut{ (DiceRule)usable[0], any5}; }

        int upperSum=0; for(int i=0;i<6;i++) if(myState.ruleScore[i]!=-1) upperSum+=myState.ruleScore[i];
        int remU=0, remL=0; for(int i=0;i<6;i++) if(myState.ruleScore[i]==-1) remU++; for(int i=6;i<12;i++) if(myState.ruleScore[i]==-1) remL++;
        double upperNeedN = max(0, 63000 - upperSum)/63000.0; double rU=remU/6.0, rL=remL/6.0;
        vector<PutCtx> ctxs(cs.size(), PutCtx{upperNeedN, rU, rL});

        int sel = rl.choosePut(cs, ctxs);
        return DicePut{ cs[sel].r, cs[sel].use };
    }

    void updateGet(vector<int> diceA, vector<int> diceB, Bid myBid, Bid oppBid, char myGroup){ if(myGroup=='A') myState.addDice(diceA), oppState.addDice(diceB); else myState.addDice(diceB), oppState.addDice(diceA);
        bool myOk = myBid.group==myGroup; myState.bid(myOk, myBid.amount);
        char oppGroup = (myGroup=='A'?'B':'A'); bool oppOk = oppBid.group==oppGroup; oppState.bid(oppOk, oppBid.amount);
        // ★ 금액 동률이면(테스터의 0/1 타이브레이커로만 결정된 라운드) 학습은 스킵하고 결과만 반영
        bool tieAmount = (myBid.amount == oppBid.amount);
        if (!tieAmount) {
            rl.observeBidOutcome(myOk, myBid.amount);
        } else if (rl.verbose) {
            cerr << "[BID-UPDATE] skipped due to tie (amount=" << myBid.amount << ")\n";
        }
    }
    void updatePut(const DicePut& put){ myState.useDice(put); rl.observePutReward(myState.ruleScore[put.rule]); }
    void updateSet(const DicePut& put){ oppState.useDice(put); }
};

// ===================== 메인 =====================
int main(){ ios::sync_with_stdio(false); cin.tie(nullptr);
    Game game;
    // 시작 시 data.bin 자동 로드(있으면)
    game.rl.loadFromFile("data.bin");
        
    
    

    vector<int> diceA, diceB; Bid myBid{'A',0}; string line;
    while(true){ if(!getline(cin,line)) break; if(line.empty()) continue; istringstream iss(line); string cmd; if(!(iss>>cmd)) continue;
        if(cmd=="READY"){ cout<<"OK"<<endl; continue; }
        if(cmd=="VERBOSE"){ string s; iss>>s; bool on = (s=="ON"||s=="on"||s=="1"||s=="TRUE"||s=="true"); game.rl.verbose=on; cout<<"OK"<<endl; continue; }
        if(cmd=="DUMP"){ cout<<"MODEL "<<game.rl.dumpJSON()<<endl; continue; }
        if(cmd=="DUMP_TXT"){ cout<<"MODEL_TXT_BEGIN\n"<<game.rl.saveText()<<"MODEL_TXT_END\n"; continue; }
        if(cmd=="RELOAD"){ if(game.rl.loadFromFile("data.bin")){ cout<<"OK\n"; cerr<<"[MODEL] reloaded from data.bin\n"; } else { cout<<"NOFILE\n"; cerr<<"[MODEL] reload failed\n"; } continue; }

        if(cmd=="ROLL"){ string strA,strB; iss>>strA>>strB; diceA.clear(); diceB.clear(); for(char c: strA) if('0'<=c&&c<='9') diceA.push_back(c-'0'); for(char c: strB) if('0'<=c&&c<='9') diceB.push_back(c-'0'); myBid=game.calculateBid(diceA,diceB); cout<<"BID "<<myBid.group<<" "<<myBid.amount<<endl; continue; }
        if(cmd=="GET"){ char getG, oppG; int oppScore; iss>>getG>>oppG>>oppScore; game.updateGet(diceA,diceB,myBid,Bid{oppG,oppScore},getG); continue; }
        if(cmd=="SCORE"){ DicePut put=game.calculatePut(); game.updatePut(put); cout<<"PUT "<<toString(put.rule)<<" "; for(int d: put.dice) cout<<d; cout<<endl; continue; }
        if(cmd=="SET"){ string rule,str; iss>>rule>>str; vector<int> ds; for(char c: str) if('0'<=c&&c<='9') ds.push_back(c-'0'); game.updateSet(DicePut{fromString(rule), ds}); continue; }
        if(cmd=="FINISH") break;
        cerr<<"Invalid command: "<<cmd<<endl;
    }
    return 0;
}