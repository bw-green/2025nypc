#include <bits/stdc++.h>
#include <fstream>
#include <filesystem>
#include <random>
using namespace std;

// =============================
// 점수 규칙
// =============================
enum DiceRule {
    ONE, TWO, THREE, FOUR, FIVE, SIX,
    CHOICE, FOUR_OF_A_KIND, FULL_HOUSE,
    SMALL_STRAIGHT, LARGE_STRAIGHT, YACHT
};

static int calcScore(DiceRule rule, const vector<int>& dice){
    auto c=[&](int v){ return (int)count(dice.begin(),dice.end(),v); };
    switch(rule){
        case ONE:return c(1)*1000; case TWO:return c(2)*2000; case THREE:return c(3)*3000;
        case FOUR:return c(4)*4000; case FIVE:return c(5)*5000; case SIX:return c(6)*6000;
        case CHOICE:return accumulate(dice.begin(),dice.end(),0)*1000;
        case FOUR_OF_A_KIND:{
            array<int,7> cc{}; for(int d:dice) if(1<=d&&d<=6) cc[d]++;
            bool ok=false; for(int i=1;i<=6;i++) if(cc[i]>=4){ok=true;break;}
            return ok? accumulate(dice.begin(),dice.end(),0)*1000:0;
        }
        case FULL_HOUSE:{
            array<int,7> cc{}; for(int d:dice) if(1<=d&&d<=6) cc[d]++;
            bool pr=false,tr=false;
            for(int i=1;i<=6;i++){ int x=cc[i]; if(x==2||x==5) pr=true; if(x==3||x==5) tr=true; }
            return (pr&&tr)? accumulate(dice.begin(),dice.end(),0)*1000:0;
        }
        case SMALL_STRAIGHT:{
            auto h=[&](int v){return c(v)>0;};
            bool ok=(h(1)&&h(2)&&h(3)&&h(4))||(h(2)&&h(3)&&h(4)&&h(5))||(h(3)&&h(4)&&h(5)&&h(6));
            return ok?15000:0;
        }
        case LARGE_STRAIGHT:{
            auto h=[&](int v){return c(v)>0;};
            bool ok=(h(1)&&h(2)&&h(3)&&h(4)&&h(5))||(h(2)&&h(3)&&h(4)&&h(5)&&h(6));
            return ok?30000:0;
        }
        case YACHT:{
            array<int,7> cc{}; for(int d:dice) if(1<=d&&d<=6) cc[d]++;
            for(int i=1;i<=6;i++) if(cc[i]==5) return 50000;
            return 0;
        }
    }
    return 0;
}

// =============================
// 상태
// =============================
struct SimState {
    vector<int> dice;
    array<int,7> cnt{};
    vector<int> ruleScore; // -1=미사용, 그 외는 사용된 점수
    SimState(): ruleScore(12,-1) {}
    void addDice(const vector<int>& nd){
        dice.insert(dice.end(), nd.begin(), nd.end());
        for(int d: nd) if(1<=d&&d<=6) cnt[d]++;
    }
    int basicSum() const{ int s=0; for(int i=0;i<6;i++) if(ruleScore[i]!=-1) s+=ruleScore[i]; return s; }
    int combSum()  const{ int s=0; for(int i=6;i<12;i++) if(ruleScore[i]!=-1) s+=ruleScore[i]; return s; }
    int bonus()    const{ return basicSum()>=63000?35000:0; }
    int total()    const{ return basicSum()+bonus()+combSum(); }
};

// =============================
// 후보 생성 (필터링 없음, 0점 후보도 포함)
// =============================
static vector<vector<int>> fillToFive(const array<int,7>& C, vector<int> core){
    array<int,7> T=C;
    for(int d: core){ if(!(1<=d&&d<=6) || T[d]==0) return {}; T[d]--; }
    while(core.size()<5){
        int v=-1;
        for(int x=6;x>=1;--x) if(T[x]>0){ v=x; break; }
        if(v==-1) break;
        core.push_back(v); T[v]--;
    }
    if(core.size()!=5) return {};
    return {core};
}

static vector<pair<DiceRule, vector<int>>> buildCandidates(const SimState& st){
    const auto &C = st.cnt;
    vector<pair<DiceRule, vector<int>>> out;
    auto push=[&](DiceRule r, const vector<int>& d){
        if(st.ruleScore[(int)r]==-1) out.push_back({r,d});
    };

    auto ok5=[&](initializer_list<int> s){ for(int v:s) if(C[v]==0) return false; return true; };
    if(ok5({1,2,3,4,5})){
        auto v = fillToFive(C, {1,2,3,4,5}); for(auto& d:v){
            push(LARGE_STRAIGHT,d); push(SMALL_STRAIGHT,d); push(CHOICE,d);
            for(int face=1; face<=6; ++face) push((DiceRule)(face-1), d);
            push(FOUR_OF_A_KIND,d); push(FULL_HOUSE,d); push(YACHT,d);
        }
    }
    if(ok5({2,3,4,5,6})){
        auto v = fillToFive(C, {2,3,4,5,6}); for(auto& d:v){
            push(LARGE_STRAIGHT,d); push(SMALL_STRAIGHT,d); push(CHOICE,d);
            for(int face=1; face<=6; ++face) push((DiceRule)(face-1), d);
            push(FOUR_OF_A_KIND,d); push(FULL_HOUSE,d); push(YACHT,d);
        }
    }
    for(int v=1; v<=6; ++v){
        if(C[v]>=5){
            auto u = fillToFive(C, {v,v,v,v,v}); for(auto& d:u){
                push(YACHT,d); push(FOUR_OF_A_KIND,d); push(FULL_HOUSE,d);
                push(CHOICE,d); for(int face=1; face<=6; ++face) push((DiceRule)(face-1), d);
                push(SMALL_STRAIGHT,d); push(LARGE_STRAIGHT,d);
            }
        } else if(C[v]>=4){
            for(int ex=6; ex>=1; --ex) if(ex==v ? C[ex]>=5 : C[ex]>0){
                auto u = fillToFive(C, {v,v,v,v,ex}); for(auto& d:u){
                    push(FOUR_OF_A_KIND,d); push(FULL_HOUSE,d); push(CHOICE,d);
                    for(int face=1; face<=6; ++face) push((DiceRule)(face-1), d);
                    push(SMALL_STRAIGHT,d); push(LARGE_STRAIGHT,d); push(YACHT,d);
                }
                break;
            }
        }
    }
    auto ok4=[&](initializer_list<int> s){ for(int v:s) if(C[v]==0) return false; return true; };
    if(ok4({1,2,3,4})){
        auto v = fillToFive(C, {1,2,3,4}); for(auto& d:v){
            push(SMALL_STRAIGHT,d); push(CHOICE,d); for(int face=1; face<=6; ++face) push((DiceRule)(face-1), d);
        }
    }
    if(ok4({2,3,4,5})){
        auto v = fillToFive(C, {2,3,4,5}); for(auto& d:v){
            push(SMALL_STRAIGHT,d); push(CHOICE,d); for(int face=1; face<=6; ++face) push((DiceRule)(face-1), d);
        }
    }
    if(ok4({3,4,5,6})){
        auto v = fillToFive(C, {3,4,5,6}); for(auto& d:v){
            push(SMALL_STRAIGHT,d); push(CHOICE,d); for(int face=1; face<=6; ++face) push((DiceRule)(face-1), d);
        }
    }

    if(out.empty()){
        vector<int> top5; for(int v=6; v>=1 && (int)top5.size()<5; --v){
            int t=min(C[v], 5-(int)top5.size()); while(t--) top5.push_back(v);
        }
        if((int)top5.size()==5){
            push(CHOICE, top5);
            for(int face=1; face<=6; ++face) push((DiceRule)(face-1), top5);
            push(SMALL_STRAIGHT, top5); push(LARGE_STRAIGHT, top5);
            push(FOUR_OF_A_KIND, top5); push(FULL_HOUSE, top5); push(YACHT, top5);
        }
    }
    return out; // ★ 필터링 없음(0점 포함)
}

// =============================
// 피처 (세트/배치)
// =============================
struct SimpleStats {
    int sum=0,cnt[7]{},maxCount=0,secondMax=0,distinct=0,longestRun=0;
};
static int runLen(const int c[7]){ int best=0,cur=0; for(int v=1;v<=6;++v){ if(c[v]>0){cur++; best=max(best,cur);} else cur=0; } return best; }
static SimpleStats statsOf(const vector<int>& d){
    SimpleStats s; for(int x:d){ if(1<=x&&x<=6){ s.sum+=x; s.cnt[x]++; } }
    for(int v=1;v<=6;++v){
        if(s.cnt[v]>0) s.distinct++;
        if(s.cnt[v]>s.maxCount){ s.secondMax=s.maxCount; s.maxCount=s.cnt[v]; }
        else s.secondMax=max(s.secondMax,s.cnt[v]);
    }
    s.longestRun=runLen(s.cnt); return s;
}

static vector<double> buildSetFeat(const vector<int>& A, const vector<int>& B, const SimState& st){
    auto SA = statsOf(A), SB = statsOf(B);
    auto norm=[&](int x,int m){return (double)x/(double)m;};
    double upperSum = 0; for(int i=0;i<6;i++) if(st.ruleScore[i]!=-1) upperSum += st.ruleScore[i];
    double upperNeed = max(0.0, 63000.0 - upperSum)/63000.0;
    int remU=0, remL=0; for(int i=0;i<6;i++) if(st.ruleScore[i]==-1) remU++; for(int i=6;i<12;i++) if(st.ruleScore[i]==-1) remL++;
    vector<double> f;
    f.push_back(1.0); // bias
    f.push_back(SA.sum/30.0); f.push_back(SB.sum/30.0); f.push_back((SA.sum-SB.sum)/30.0);
    f.push_back(norm(SA.maxCount,5)); f.push_back(norm(SB.maxCount,5)); f.push_back((SA.maxCount-SB.maxCount)/5.0);
    f.push_back(norm(SA.distinct,5)); f.push_back(norm(SB.distinct,5)); f.push_back((SA.distinct-SB.distinct)/5.0);
    f.push_back(norm(SA.longestRun,5)); f.push_back(norm(SB.longestRun,5)); f.push_back((SA.longestRun-SB.longestRun)/5.0);
    for(int v=1;v<=6; ++v) f.push_back(st.cnt[v]/30.0); // 현재 보유
    f.push_back(upperSum/63000.0); f.push_back(upperNeed); f.push_back(remU/6.0); f.push_back(remL/6.0);
    return f; // 23
}

// PUT 정책 피처: 점수만
static vector<double> buildPutFeat_scoreOnly(DiceRule r, const vector<int>& fiveDice){
    int sc = calcScore(r, fiveDice);
    vector<double> f; f.reserve(2);
    f.push_back(1.0);
    f.push_back(sc / 50000.0);
    return f; // 2차원
}

// =============================
// 선형 정책
// =============================
struct LinearPolicy {
    vector<double> w;
    double lr=0.05, temp=1.8, entropy_beta=0.01, l2=1e-4, clip=0.1, baseline=0.0, gamma=0.99;
    LinearPolicy(int D=0): w(D,0.0) {}
    static double dot(const vector<double>& a, const vector<double>& b){ double s=0; for(size_t i=0;i<a.size();++i) s+=a[i]*b[i]; return s; }
    static vector<double> softmaxTemp(const vector<double>& z, double tau){
        double m=*max_element(z.begin(),z.end()); vector<double> e(z.size()); double s=0; double t=max(1e-8,tau);
        for(size_t i=0;i<z.size();++i){ e[i]=exp((z[i]-m)/t); s+=e[i]; } for(double &v:e) v/= (s>0?s:1); return e;
    }
    static void clipVec(vector<double>& g, double maxn){
        if(maxn<=0) return; double n=0; for(double v:g) n+=v*v; n=sqrt(n);
        if(n>maxn){ double s=maxn/(n+1e-12); for(double &v:g) v*=s; }
    }
    pair<double,double> probs2(const vector<double>& x) const {
        double a = dot(w,x);
        vector<double> z = {+a, -a};
        auto p = softmaxTemp(z, temp);
        return {p[0], p[1]};
    }
    void update2(const vector<double>& x, int chosenIdx, double reward_norm){
        auto p = probs2(x);
        double H=0.0; for(double q: {p.first,p.second}) if(q>0) H -= q*log(q);
        double r_eff = reward_norm + entropy_beta*H;
        double pL=p.first;
        double coeff = (chosenIdx==0)? (1.0 - 2.0*pL) : (-2.0*pL);
        vector<double> g(x.size());
        for(size_t i=0;i<x.size();++i) g[i] = (r_eff - baseline) * coeff * x[i];
        clipVec(g, clip);
        for(size_t i=0;i<w.size();++i) w[i] += lr * g[i];
        for(double &v: w) v -= lr*l2*v;
        baseline = gamma*baseline + (1.0-gamma)*reward_norm;
    }
};

// =============================
// 학습기
// =============================
struct Learner {
    LinearPolicy setPol; // 23
    LinearPolicy putPol; // 2
    Learner(): setPol(23), putPol(2) {}
};

// =============================
// 랜덤 초기화 + data2.bin 저장/로드
// =============================
static void initRandomParams(Learner& L, unsigned seed=0){
    if(seed==0) seed = std::random_device{}();
    std::mt19937 rng(seed);
    std::normal_distribution<double> nd(0.0, 0.05);
    for(double &w : L.setPol.w) w = nd(rng);
    for(double &w : L.putPol.w) w = nd(rng);
}

static bool saveParamsBinary(const Learner& L, const std::string& path){
    std::ofstream fout(path, std::ios::binary);
    if(!fout) return false;
    const char magic[8] = {'R','L','P','v','0','0','0','1'};
    fout.write(magic, 8);
    int setDim = (int)L.setPol.w.size();
    fout.write(reinterpret_cast<const char*>(&setDim), sizeof(int));
    fout.write(reinterpret_cast<const char*>(L.setPol.w.data()), sizeof(double)*setDim);
    int putDim = (int)L.putPol.w.size();
    fout.write(reinterpret_cast<const char*>(&putDim), sizeof(int));
    fout.write(reinterpret_cast<const char*>(L.putPol.w.data()), sizeof(double)*putDim);
    return (bool)fout;
}

static bool loadParamsBinary(Learner& L, const std::string& path){
    std::ifstream fin(path, std::ios::binary);
    if(!fin) return false;
    char magic[8]; fin.read(magic, 8);
    if(std::string(magic, magic+8)!="RLPv0001") return false;
    int setDim=0; fin.read(reinterpret_cast<char*>(&setDim), sizeof(int));
    if(setDim != (int)L.setPol.w.size()) return false;
    fin.read(reinterpret_cast<char*>(L.setPol.w.data()), sizeof(double)*setDim);
    int putDim=0; fin.read(reinterpret_cast<char*>(&putDim), sizeof(int));
    if(putDim != (int)L.putPol.w.size()) return false;
    fin.read(reinterpret_cast<char*>(L.putPol.w.data()), sizeof(double)*putDim);
    return (bool)fin;
}

// =============================
// 라운드·로그·시뮬
// =============================
static bool is5digits(const string& s){
    if(s.size()!=5) return false;
    for(char c: s) 
        if(c<'0'||c>'9') return false;
    return true; // 모든 조건 통과하면 true
}

struct RoundIn { vector<int> A,B; int tie; };

struct StepLogSet { vector<double> fx; int chosenLR; bool learn; };
struct StepLogPut {
    vector<vector<double>> feats;
    int selIdx;
    vector<pair<DiceRule,vector<int>>> cs;
    StepLogPut(): selIdx(-1) {}          // ★ 기본값 안전화
};

// PUT 실행(피처는 scoreOnly)
static void doPut(SimState& st, Learner& L, StepLogPut* slog=nullptr){
    auto cs = buildCandidates(st);
    if(cs.empty()){
        if(slog){                          // ★ 빈 로그도 push할 수 있게 기본 상태 유지
            slog->feats.clear();
            slog->cs.clear();
            slog->selIdx = -1;
        }
        return;
    }

    vector<double> z(cs.size(),0.0);
    vector<vector<double>> feats(cs.size());
    for(size_t i=0;i<cs.size();++i){
        feats[i] = buildPutFeat_scoreOnly(cs[i].first, cs[i].second);
        z[i] = LinearPolicy::dot(L.putPol.w, feats[i]);
    }
    int sel=0; for(size_t i=1;i<cs.size();++i) if(z[i]>z[sel]) sel=(int)i;

    if(slog){ slog->feats = feats; slog->selIdx = sel; slog->cs = cs; }

    array<int,7> tmp = st.cnt;
    for(int d: cs[sel].second){ if(1<=d&&d<=6) tmp[d]--; }
    st.cnt = tmp;
    st.ruleScore[(int)cs[sel].first] = calcScore(cs[sel].first, cs[sel].second);
}

static void simulateEpisode(
    Learner& L, const vector<RoundIn>& R,
    int overrideSetAt, // -1: 없음, t: 세트 반대로
    vector<StepLogSet>* setLogs,
    vector<StepLogPut>* putLogs,
    int &finalScore,
    bool collectLogs
){
    SimState st;
    if(collectLogs){ setLogs->clear(); putLogs->clear(); setLogs->reserve(R.size()); putLogs->reserve(R.size()); }

    for(int t=0; t<(int)R.size(); ++t){
        const auto& rd = R[t];
        auto fx = buildSetFeat(rd.A, rd.B, st);
        auto pr = L.setPol.probs2(fx);
        int want = (pr.second>pr.first?1:0);
        if(t==overrideSetAt) want ^= 1;
        int take = want ^ (rd.tie==1?1:0);
        if(take==0) st.addDice(rd.A); else st.addDice(rd.B);

        if(collectLogs) setLogs->push_back({fx, want, rd.tie==0});

        StepLogPut plog;                  // ★ 항상 하나 만들고
        doPut(st, L, collectLogs? &plog: nullptr);
        if(collectLogs) putLogs->push_back(plog);   // ★ 항상 push -> 크기 보장
    }
    finalScore = st.total();
}

// =============================
// 메인
// =============================
int main(){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    Learner L;
    // HP
    L.setPol.lr=0.05; L.setPol.temp=1.8; L.setPol.entropy_beta=0.01; L.setPol.l2=1e-4; L.setPol.clip=0.1;
    L.putPol.lr=0.05; L.putPol.temp=1.0; L.putPol.entropy_beta=0.0;  L.putPol.l2=1e-4; L.putPol.clip=0.1;

    // data2.bin 존재시 로드, 없으면 랜덤 생성 후 저장
    const std::string path = "data2.bin";
    if(std::filesystem::exists(path)){
        if(!loadParamsBinary(L, path)){
            std::cerr << "data2.bin 로드 실패. 랜덤 초기화.\n";
            initRandomParams(L); saveParamsBinary(L, path);
        }
    } else {
        initRandomParams(L); saveParamsBinary(L, path);
    }

    vector<RoundIn> rounds; rounds.reserve(12);
    SimState outSt;
    cout.setf(ios::fixed); cout<<setprecision(6);

    string As,Bs; int tie;
    while (cin>>As>>Bs>>tie){
        if(!is5digits(As)||!is5digits(Bs)) continue;
        RoundIn rd; for(char c:As) rd.A.push_back(c-'0'); for(char c:Bs) rd.B.push_back(c-'0'); rd.tie=tie;
        rounds.push_back(rd);

        // 즉시 확률/선택/점수 출력
        auto fx = buildSetFeat(rd.A, rd.B, outSt);
        auto pr = L.setPol.probs2(fx);
        int want = (pr.second>pr.first?1:0);
        char ch = (want==0?'L':'R');
        int take = want ^ (tie==1?1:0);
        if(take==0) outSt.addDice(rd.A); else outSt.addDice(rd.B);

        doPut(outSt, L, nullptr); // scoreOnly 기반 argmax

        cout << pr.first << " " << pr.second << " " << ch << " " << outSt.total() << "\n";

        // 12라운드 종료 → 역방향 학습 + 저장
        if((int)rounds.size()==12){
            vector<StepLogSet> setLogs; vector<StepLogPut> putLogs; int S_actual=0;
            simulateEpisode(L, rounds, -1, &setLogs, &putLogs, S_actual, true);

            const int T=12;
            for(int t=T-1; t>=0; --t){
                // ★ 대안 경로도 collectLogs=true로! (세그폴트 방지)
                vector<StepLogSet> dummyS; vector<StepLogPut> dummyP; int S_cf=0;
                simulateEpisode(L, rounds, t, &dummyS, &dummyP, S_cf, true);

                // 방어적 크기 체크
                if((int)putLogs.size()!=T || (int)dummyP.size()!=T) {
                    std::cerr << "log size mismatch\n";
                    continue;
                }

                // 세트 정책 업데이트
                if(setLogs[t].learn){
                    double adv = (double)(S_actual - S_cf)/50000.0;
                    double wr = pow(0.95, T-1 - t);
                    L.setPol.update2(setLogs[t].fx, setLogs[t].chosenLR, adv*wr);
                }

                // PUT 정책 업데이트 (실제 t)
                if(putLogs[t].selIdx>=0 && !putLogs[t].feats.empty()){
                    auto &feats = putLogs[t].feats; int K=feats.size();
                    vector<double> z(K); for(int k=0;k<K;++k) z[k] = LinearPolicy::dot(L.putPol.w, feats[k]);
                    auto p = LinearPolicy::softmaxTemp(z, 1.0);
                    vector<double> expect(L.putPol.w.size(),0.0);
                    for(int k=0;k<K;++k) for(size_t d=0; d<expect.size(); ++d) expect[d]+= p[k]*feats[k][d];
                    vector<double> g(L.putPol.w.size(),0.0);
                    double Rn = (double)S_actual/50000.0;
                    for(size_t d=0; d<g.size(); ++d) g[d] = (feats[putLogs[t].selIdx][d] - expect[d]) * (Rn - L.putPol.baseline);
                    LinearPolicy::clipVec(g, L.putPol.clip);
                    for(size_t d=0; d<g.size(); ++d) L.putPol.w[d] += L.putPol.lr * g[d];
                    for(double &v: L.putPol.w) v -= L.putPol.lr * L.putPol.l2 * v;
                    L.putPol.baseline = L.putPol.gamma*L.putPol.baseline + (1.0-L.putPol.gamma)*Rn;
                }

                // PUT 정책 업데이트 (대안 t)
                if(dummyP[t].selIdx>=0 && !dummyP[t].feats.empty()){
                    auto &feats = dummyP[t].feats; int K=feats.size();
                    vector<double> z(K); for(int k=0;k<K;++k) z[k] = LinearPolicy::dot(L.putPol.w, feats[k]);
                    auto p = LinearPolicy::softmaxTemp(z, 1.0);
                    vector<double> expect(L.putPol.w.size(),0.0);
                    for(int k=0;k<K;++k) for(size_t d=0; d<expect.size(); ++d) expect[d]+= p[k]*feats[k][d];
                    vector<double> g(L.putPol.w.size(),0.0);
                    double Rn = (double)(-S_cf)/50000.0;
                    for(size_t d=0; d<g.size(); ++d) g[d] = (feats[dummyP[t].selIdx][d] - expect[d]) * (Rn - L.putPol.baseline);
                    LinearPolicy::clipVec(g, L.putPol.clip);
                    for(size_t d=0; d<g.size(); ++d) L.putPol.w[d] += L.putPol.lr * g[d];
                    for(double &v: L.putPol.w) v -= L.putPol.lr * L.putPol.l2 * v;
                    L.putPol.baseline = L.putPol.gamma*L.putPol.baseline + (1.0-L.putPol.gamma)*Rn;
                }
            }

            // ★ 학습 후 저장
            saveParamsBinary(L, path);

            // 다음 에피소드 준비
            rounds.clear();
            outSt = SimState{};
        }
    }

    // ★ 프로그램 종료 전 한 번 더 저장(안전)
    saveParamsBinary(L, path);
    return 0;
}
