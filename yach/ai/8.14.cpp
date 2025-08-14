#include <algorithm>
#include <cassert>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <deque>
#include <functional>
#include <climits>
#include <utility>
#include <cmath>
using namespace std;
constexpr int SMALL_STRAIGHT_SCORE = 15000;
constexpr int LARGE_STRAIGHT_SCORE = 30000;
constexpr int YACHT_SCORE          = 50000;

// 상단 보너스 기준/보너스
constexpr int UPPER_TARGET = 63000;
constexpr int UPPER_BONUS  = 35000;

// 팩 평균으로 저눈/고눈 판정 (입찰에서 사용, 필요시 PUT 로직에서도 활용 가능)
constexpr double MEAN_LOW  = 2.2;
constexpr double MEAN_HIGH = 4.2;


// ---------- [PUT ONLY | 배치 전략 전용] ----------
constexpr int BOOST_TRIPLE_TO_UPPER   =  7000;   // 상단에 3개 이상이면 살짝 가점
constexpr int KILL_FH_IF_TRIPLE_OPEN  = 100000;  // 트리플 상단 비어있을 때 풀하우스 사실상 금지(초중반)
constexpr int EXTRA_KILL_IF_USE_2_5   =  10000;  // 풀하우스가 2~5(스트레이트 핵심)를 소모하면 추가 감점
constexpr int CHOICE_4K_LOW_SUM_PENALTY = 5000;  // 합계 낮을 때 CHOICE/4K 약화(상단 유도)

// 보너스 포기(덤프) 성향: 저눈을 우선 소모
constexpr int DUMP_FACE_PENALTY_PER_PIP = 3000;  // 큰 눈일수록 감점
constexpr int DUMP_ONE_EXTRA_BONUS      = 6000;  // ONE에 버리면 추가 가산

// 후반(남은 칸 적음) 가중 부스트
inline constexpr int LATE4K_BOOST[4] = { 0, 7000, 11000, 15000 }; // 4kind
inline constexpr int LATEFH_BOOST[4] = { 0, 6000,  9000, 12000 }; // Full House


// ---------- [BID ONLY | 입찰 전용] ----------
// 절대 하드캡
constexpr int MAX_BID = 100000;

// 1라운드: 6의 개수에 따른 초기 투자
constexpr int INIT_BID_BY_SIX[6] = { 778, 1296, 2160, 3600, 6000, 10000 };

// 1~9R은 고정 상한(초기 투자 구간)
constexpr int EARLY_ROUND_LIMIT   = 9;
constexpr int EARLY_ROUND_MAX_BID = 5001;

// (선택) 10R~ 동적 상한 계산용 파라미터 — capByRound에서 사용
constexpr int OPP_CAP_MULT   = 2;   // 상대 과거 최대 * 2
constexpr int OPP_CAP_OFFSET = 10;  // + 10

// 선호 판단 최소 격차
constexpr int PREF_GAP = 2000;

// 스윙 기반 지불의향 WTP 가중치(상황적 공격성 계수)
constexpr double SELF_W_BASE     = 0.75;
constexpr double OPP_W_BASE      = 0.72;
constexpr double SCORE_SCALE     = 70000.0; // 점수 차 정규화 스케일
constexpr double SELF_W_NEG_GAIN = 0.15;    // 내가 뒤질 때 가산
constexpr double SELF_W_POS_LOSS = 0.12;    // 내가 앞설 때 감산
constexpr double OPP_W_POS_GAIN  = 0.12;    // 상대가 뒤질 때(=내가 앞설 때) 상대 가산
constexpr double OPP_W_NEG_LOSS  = 0.10;    // 상대가 앞설 때 상대 감산
constexpr double SELF_W_MIN      = 0.55;
constexpr double SELF_W_MAX      = 0.98;
constexpr double OPP_W_MIN       = 0.50;
constexpr double OPP_W_MAX       = 0.95;

// 보수화: 스윙 데드존(치명 위협 없을 때 이 이하면 0원)
constexpr long long SW_PASSIVE_EARLY = 16000; // 1~6R
constexpr long long SW_PASSIVE_MID   = 14000; // 7~9R
constexpr long long SW_PASSIVE_LATE  =  9000; // 10R~

// 점수 상황별 공격성 스케일 (myWTP 자체 축소)
constexpr double AGGR_LEAD_SCALE   = 0.65; // 리드 중
constexpr double AGGR_TIE_SCALE    = 0.72; // 비등
constexpr double AGGR_BEHIND_SCALE = 0.82; // 뒤짐(그래도 보수적)

// 초반(1~6R) 추가 보수화 배수
constexpr double EARLY_ROUND_AGGR_MULT = 0.90;

// 미세 오버비드(상대예상+ε) / 습관치+δ — 축소 버전
constexpr int    EPSILON_MIN    = 50;
constexpr int    EPSILON_MAX    = 600;
constexpr double EPSILON_RATIO  = 0.012; // SW의 1.2%

constexpr int    HABIT_DELTA_MIN   = 50;
constexpr int    HABIT_DELTA_MAX   = 500;
constexpr double HABIT_DELTA_RATIO = 0.010; // SW의 1.0%

// 상대 저액 패턴에 따른 동적 상한 보수화
constexpr int    AVG_LOW_CAP_THRESHOLD = 2000;   // 상대 평균 입찰이 이 값 미만이면
constexpr int    AVG_LOW_CAP           = 3000;   // 우리 상한을 3000으로 캡
constexpr int    NEAR_ZERO_THRESH      = 200;    // |bid| <= 200 를 '0 근처'로 간주
constexpr double ZERO_RATE_MIN_FRAC    = 0.60;   // 0 근처 비율이 60% 이상이면
constexpr int    ZERO_RATE_CAP         = 1000;   // 우리 상한을 1000으로 더 낮춤

// 상대가 평균 2000 미만으로 던지면 late 캡 3000
constexpr int   OPPO_LOW_AVG_THRESH  = 2000;
constexpr int   ULTRA_LOW_CAP        = 3000;
constexpr int FINAL_SAFE_MARGIN = 10;
// 가능한 주사위 규칙들을 나타내는 enum
enum DiceRule {
    ONE, TWO, THREE, FOUR, FIVE, SIX,
    CHOICE, FOUR_OF_A_KIND, FULL_HOUSE, SMALL_STRAIGHT, LARGE_STRAIGHT, YACHT
};

// 입찰 방법을 나타내는 구조체
struct Bid {
    char group;  // 입찰 그룹 ('A' 또는 'B')
    int amount;  // 입찰 금액
};

// 주사위 배치 방법을 나타내는 구조체
struct DicePut {
    DiceRule rule;     // 배치 규칙
    vector<int> dice;  // 배치할 주사위 목록(항상 길이 5 출력)
};

// 팀의 현재 상태를 관리하는 구조체
struct GameState {
    vector<int> dice;        // 출력/호환용(실제 소비는 cnt로 처리)
    array<int,7> cnt{};      // 1..6 주사위 개수(진실 소스)
    vector<int> ruleScore;   // 각 규칙별 획득 점수 (사용하지 않았다면 -1)
    int bidScore;            // 입찰로 얻거나 잃은 총 점수
    

    GameState() : ruleScore(12, -1), bidScore(0) {}

    int getTotalScore() const;                 // 현재까지 획득한 총 점수 계산
    void bid(bool isSuccessful, int amount);   // 입찰 결과 반영
    void addDice(const vector<int>& newDice);  // 주사위 추가
    void useDice(const DicePut& put);          // 주사위 사용(배치)
    static int calculateScore(DiceRule rule, const vector<int>& dice); // 점수 계산
};

// 게임 상태를 관리하는 클래스
class Game {
   public:
    GameState myState;   // 내 팀 상태
    GameState oppState;  // 상대 팀 상태
    deque<DicePut> plannedTwo; 
    int roundNo = 0;
    int lastMyBid = 0;
    int lastOppBid = 0;
    long long oppBidSum = 0;
    int oppBidCount = 0;
    deque<int> myBidHist, oppBidHist;

    
    static inline bool canCrossUpperNow(const GameState& st, const std::array<int,7>& cnt) {
    // 현재까지 상단 누적 점수
    int cur = 0;
    for (int i = 0; i < 6; ++i)
        if (st.ruleScore[i] != -1) cur += st.ruleScore[i];

    if (cur >= 63000) return false; // 이미 보너스 받았으면 해당 없음

    // 아직 비어 있는 상단 칸들 중 하나를 이번 주사위로 채웠을 때 63k를 넘는지?
    for (int f = 1; f <= 6; ++f) {
        if (st.ruleScore[f-1] == -1) {
            int add = std::min(cnt[f], 5) * f * 1000; // 그 얼굴로 얻는 상단 점수
            if (cur + add >= 63000) return true;
        }
    }
    return false;
    }
    pair<DicePut,DicePut> planFinalTwoExhaustive() const;
    // ======== 입찰 전략 ========
    
   Bid calculateBid(const vector<int>& diceA, const vector<int>& diceB) {
    ++roundNo;

    // 동적 상한 (라운드/상대 히스토리 반영)
    auto capByRound = [&](long long x)->int {
        long long capLate = MAX_BID;

        if (!oppBidHist.empty()) {
            int oppMax = *max_element(oppBidHist.begin(), oppBidHist.end());
            double oppAvg = accumulate(oppBidHist.begin(), oppBidHist.end(), 0.0) / oppBidHist.size();

            // 평균적으로 2000 미만이면 late 캡을 3000으로 강제
            if (oppAvg < OPPO_LOW_AVG_THRESH) capLate = min<long long>(capLate, ULTRA_LOW_CAP);

            // late 상한은 상대 과거 최대 *2 + 10
            capLate = min<long long>(capLate, (long long)oppMax * 2 + 10);
        }

        long long cap = (roundNo <= EARLY_ROUND_LIMIT) ? EARLY_ROUND_MAX_BID : capLate;
        if(roundNo>=13)cap+5000;
        if (x < 0) x = 0;
        if (x > cap) x = cap;
        return (int)x;
    };

    // ---------- 1라운드: 기존 전략 유지 ----------
    if (roundNo == 1) {
        auto cnt6 = [](const vector<int>& v){ return (int)count(v.begin(), v.end(), 6); };
        auto sumv = [](const vector<int>& v){ return accumulate(v.begin(), v.end(), 0); };

        int c6A = cnt6(diceA), c6B = cnt6(diceB);
        int sumA = sumv(diceA),  sumB = sumv(diceB);
        char group = (c6A != c6B) ? (c6A > c6B ? 'A' : 'B')
                                  : (sumA >= sumB ? 'A' : 'B'); // 동률 A

        int k = (group=='A' ? c6A : c6B); k = max(0, min(5, k));
        int amount = INIT_BID_BY_SIX[k];
        return Bid{group, capByRound(amount)};
    }
    // ---------- 2라운드 이상 ----------
    auto addPack = [](array<int,7> c, const vector<int>& p){
        for (int d : p) if (1<=d && d<=6) c[d]++;
        return c;
    };
    auto currUpper = [](const GameState& st){
        int s=0; for (int i=0;i<6;++i) if (st.ruleScore[i]!=-1) s+=st.ruleScore[i];
        return s;
    };
    auto bestChoice = [](const array<int,7>& c){
        int need=5,sum=0; for (int v=6; v>=1 && need>0; --v){ int t=min(need,c[v]); sum+=t*v; need-=t; }
        return sum*1000;
    };
    auto bestFourKind = [](const array<int,7>& c){
        int best=0;
        for (int v=6; v>=1; --v) if (c[v]>=4){
            int extra=0; for (int x=6; x>=1; --x){ int have=c[x]-(x==v?4:0); if (have>0){ extra=x; break; } }
            best = max(best,(4*v+extra)*1000);
        }
        return best;
    };
    auto bestFullHouse = [](const array<int,7>& c){
        for (int v=6; v>=1; --v) if (c[v]==5) return 5*v*1000;
        int best=0;
        for (int t=6; t>=1; --t) if (c[t]>=3)
            for (int p=6; p>=1; --p) if (p!=t && c[p]>=2){ best=max(best,(3*t+2*p)*1000); break; }
        return best;
    };
    auto hasSmall = [](const array<int,7>& c){
        bool a=c[1]>0&&c[2]>0&&c[3]>0&&c[4]>0;
        bool b=c[2]>0&&c[3]>0&&c[4]>0&&c[5]>0;
        bool d=c[3]>0&&c[4]>0&&c[5]>0&&c[6]>0;
        return a||b||d;
    };
    auto hasLarge = [](const array<int,7>& c){
        bool a=c[1]>0&&c[2]>0&&c[3]>0&&c[4]>0&&c[5]>0;
        bool b=c[2]>0&&c[3]>0&&c[4]>0&&c[5]>0&&c[6]>0;
        return a||b;
    };
    auto hasYacht = [](const array<int,7>& c){
        for (int f=1; f<=6; ++f) if (c[f]>=5) return true;
        return false;
    };
    auto bestUpperWithBonus = [&](const GameState& st, const array<int,7>& c){
        int cu = currUpper(st);
        int best = 0;
        for (int f=1; f<=6; ++f) if (st.ruleScore[f-1]==-1) {
            int base = min(c[f],5)*f*1000;
            int add  = (cu < UPPER_TARGET && cu + base >= UPPER_TARGET) ? UPPER_BONUS : 0;
            best = max(best, base + add);
        }
        return best;
    };
    auto bestImmediate = [&](const GameState& st, const array<int,7>& c){
        int best = 0;
        if (st.ruleScore[CHOICE]==-1)         best = max(best, bestChoice(c));
        if (st.ruleScore[FOUR_OF_A_KIND]==-1) best = max(best, bestFourKind(c));
        if (st.ruleScore[FULL_HOUSE]==-1)     best = max(best, bestFullHouse(c));
        if (st.ruleScore[SMALL_STRAIGHT]==-1 && hasSmall(c)) best = max(best, SMALL_STRAIGHT_SCORE);
        if (st.ruleScore[LARGE_STRAIGHT]==-1 && hasLarge(c)) best = max(best, LARGE_STRAIGHT_SCORE);
        if (st.ruleScore[YACHT]==-1 && hasYacht(c))          best = max(best, YACHT_SCORE);
        best = max(best, bestUpperWithBonus(st, c));
        return best;
    };

    // 카운트 after pick
    array<int,7> myC0 = myState.cnt, oppC0 = oppState.cnt;
    array<int,7> myA = addPack(myC0, diceA), myB = addPack(myC0, diceB);
    array<int,7> opA = addPack(oppC0, diceA), opB = addPack(oppC0, diceB);

    int myBest_A  = bestImmediate(myState, myA);
    int myBest_B  = bestImmediate(myState, myB);
    int oppBest_A = bestImmediate(oppState, opA);
    int oppBest_B = bestImmediate(oppState, opB);

    long long SWING_A = (long long)(myBest_A - myBest_B) + (long long)(oppBest_B - oppBest_A);
    long long SWING_B = (long long)(myBest_B - myBest_A) + (long long)(oppBest_A - oppBest_B);

    char group = (SWING_A >= SWING_B ? 'A' : 'B');
    long long SW   = (group=='A' ? SWING_A : SWING_B);
    int myBestChosen  = (group=='A' ? myBest_A  : myBest_B);
    int myBestOther   = (group=='A' ? myBest_B  : myBest_A);

    // ① 선호가 반대로 갈리면 → 소액만 (3)
    bool myPreferA  = (myBest_A  >= myBest_B  + PREF_GAP);
    bool myPreferB  = (myBest_B  >= myBest_A  + PREF_GAP);
    bool oppPreferA = (oppBest_A >= oppBest_B + PREF_GAP);
    bool oppPreferB = (oppBest_B >= oppBest_A + PREF_GAP);
    if ((myPreferA && oppPreferB) || (myPreferB && oppPreferA)) {
        char g = myPreferA ? 'A' : 'B';
        return Bid{g, capByRound(3)};
    }

    // ② 내가 선호와 반대인데 스윙도 작다 → 소액 견제 (3)
    if ((myBestOther >= myBestChosen + PREF_GAP) && SW < 12000) {
        return Bid{group, capByRound(3)};
    }

    // ③ 가치가 없으면 0
    if (SW <= 0) return Bid{group, 0};

    // ==== 히스토리 보정/상대 예측 ====
    auto clamp01 = [](double x, double lo, double hi){ return x<lo?lo:(x>hi?hi:x); };

    int lead = myState.getTotalScore() - oppState.getTotalScore();

    double selfW = SELF_W_BASE
                 + (lead<0 ? min(SELF_W_NEG_GAIN, (-lead)/SCORE_SCALE)
                           : -min(SELF_W_POS_LOSS,  (double)lead/SCORE_SCALE));
    double oppW  = OPP_W_BASE
                 + (lead>0 ? min(OPP_W_POS_GAIN,  (double)lead/SCORE_SCALE)
                           : -min(OPP_W_NEG_LOSS, (-lead)/SCORE_SCALE));
    selfW = clamp01(selfW, SELF_W_MIN, SELF_W_MAX);
    oppW  = clamp01(oppW,  OPP_W_MIN,  OPP_W_MAX);

    // 보수화 스케일 적용
    double aggrScale =
        (lead > 0) ? AGGR_LEAD_SCALE :
        (lead < 0) ? AGGR_BEHIND_SCALE : AGGR_TIE_SCALE;
    if (roundNo <= 6) aggrScale *= EARLY_ROUND_AGGR_MULT;

    long long myWTP  = (long long)((selfW * SW) * aggrScale); // ← 버그 수정: 자기 자신에 곱하던 걸 교정
    long long oppEst = (long long)(oppW  * SW);

    // 상대가 늘 낮게 내는 경향치만큼 같이 줄이기
    auto underMargin = [&]()->int{
        int n = (int)min(myBidHist.size(), oppBidHist.size());
        if (n == 0) return 0;
        vector<int> diffs; diffs.reserve(n);
        for (int i=0;i<n;++i){
            int d = myBidHist[i] - oppBidHist[i];
            if (d > 0) diffs.push_back(d);
        }
        if (diffs.empty()) return 0;
        sort(diffs.begin(), diffs.end());
        int med = diffs[diffs.size()/2];
        int avg = accumulate(diffs.begin(), diffs.end(), 0) / (int)diffs.size();
        return max(med, avg);
    };
    int under = underMargin();
    myWTP  = max(0LL, myWTP  - under);
    oppEst = max(0LL, oppEst - under);

    auto mean = [](const deque<int>& v)->double{
        if (v.empty()) return 0.0; long long s=0; for (int x: v) s+=x; return (double)s / v.size();
    };
    auto median = [](deque<int> v)->double{
        if (v.empty()) return 0.0;
        vector<int> w(v.begin(), v.end());
        nth_element(w.begin(), w.begin()+w.size()/2, w.end());
        if (w.size()%2==1) return w[w.size()/2];
        int a = *max_element(w.begin(), w.begin()+w.size()/2);
        int b = w[w.size()/2];
        return 0.5*(a+b);
    };
    auto ema = [](const deque<int>& v, double alpha)->double{
        if (v.empty()) return 0.0;
        double e = v.front();
        for (int x: v) e = alpha * x + (1.0 - alpha) * e;
        return e;
    };
    auto predictOppBid = [&]()->int{
        if (oppBidHist.empty()) return 0;
        int n = (int)oppBidHist.size();
        int near0=0, near20=0;
        for (int b: oppBidHist){ if (abs(b) <= 200) near0++; if (abs(b-20000) <= 800) near20++; }
        if (near0  >= max(3, (int)(0.6*n))) return 0;
        if (near20 >= max(3, (int)(0.6*n))) return 20000;
        double pred = 0.4*ema(oppBidHist, 0.6) + 0.3*mean(oppBidHist) + 0.3*median(oppBidHist);
        return (int)llround(pred);
    };

    int epsilon    = (int)max( (long long)EPSILON_MIN,
                         min( (long long)EPSILON_MAX, (long long)(SW * EPSILON_RATIO) ) );
    int habitDelta = (int)max( (long long)HABIT_DELTA_MIN,
                         min( (long long)HABIT_DELTA_MAX, (long long)(SW * HABIT_DELTA_RATIO) ) );
    long long target1 = oppEst + epsilon;
    long long target2 = (long long)predictOppBid() + habitDelta;
    long long targetWin = max(target1, target2);

    bool finalRound = (roundNo >= 13);
    if (finalRound) {
        // 현재 총점(입찰점수 포함) 격차
        long long myTot  = myState.getTotalScore();
        long long oppTot = oppState.getTotalScore();
        long long margin = myTot - oppTot; // >0이면 내가 앞서는 중

        // 상대가 선호 팩을 가질 때의 "위협 값" (그 팩과 비선호 팩의 차이)
        // - A/B 중 상대에게 더 큰 쪽을 주면 생기는 득점 차이를 rough 하게 본다
        long long oppThreat = llabs((long long)oppBest_A - (long long)oppBest_B);

        // 내가 낼 수 있는 '안전 견제 예산' = 지금 리드에서 최소 리드만 남기고 쓸 수 있는 금액
        long long safeSpend = max(0LL, margin - (long long)FINAL_SAFE_MARGIN);

        // 마지막 라운드에, 위협이 있고(=막을 가치가 있고) 안전 예산이 있으면
        if (oppThreat > 0 && safeSpend > 0) {
            // 상대 예상치 + ε, 상대 습관치 + δ, 그리고 내가 안전하게 쓸 수 있는 금액 중
            // '이길 수 있는 만큼만, 그리고 안전하게'로 타깃을 잡는다.
            long long safeTarget = min(myWTP, safeSpend);
            long long needToWin  = max(target1, target2);   // (oppEst+ε) vs (habit+δ)
            long long forceBid   = max(needToWin, safeTarget);

            // 과투자 방지: 최종 상한 캡 적용
            return Bid{group, capByRound(forceBid)};
        }
    }

    long long amount = min(myWTP, targetWin);
    return Bid{group, capByRound(amount)};
}





    // ======== 배치 전략 (항상 5개 출력, cnt 기반으로 빠르게) ========

DicePut calculatePut() {
    if (!plannedTwo.empty()) { auto out = plannedTwo.front(); plannedTwo.pop_front(); return out;}
    vector<int> usable;
    for (int r = 0; r < 12; ++r) if (myState.ruleScore[r] == -1) usable.push_back(r);
    if (usable.empty()) return DicePut{ONE, {}};

    int totalDice = 0; for (int v=1; v<=6; ++v) totalDice += myState.cnt[v];
    if (totalDice < 5) {
        vector<int> any5;
        auto tmp = myState.cnt;
        for (int v=6; v>=1 && (int)any5.size()<5; --v){
            while(tmp[v]>0 && (int)any5.size()<5){ any5.push_back(v); tmp[v]--; }
        }
        while((int)any5.size()<5) any5.push_back(1);
        return DicePut{ (DiceRule)usable[0], any5 };
    }

    const auto &C = myState.cnt;
    

    auto build_multi = [&](int v, int k){
        vector<int> out; out.reserve(k);
        for (int i=0;i<k;i++) out.push_back(v);
        return out;
    };

    // 콤보(라지/스몰/풀하우스) 현재 각 여부 판정
    auto selectLarge = [&](){
        int a[5]={1,2,3,4,5}, b[5]={2,3,4,5,6};
        auto ok=[&](int* s){ for(int i=0;i<5;i++) if(C[s[i]]==0) return false; return true; };
        if (ok(a)) return vector<int>{1,2,3,4,5};
        if (ok(b)) return vector<int>{2,3,4,5,6};
        return vector<int>{};
    };
    auto selectSmall = [&](){
        int a[4]={1,2,3,4}, b[4]={2,3,4,5}, c[4]={3,4,5,6};
        auto ok=[&](int* s){ for(int i=0;i<4;i++) if(C[s[i]]==0) return false; return true; };
        if (ok(a)) return vector<int>{1,2,3,4};
        if (ok(b)) return vector<int>{2,3,4,5};
        if (ok(c)) return vector<int>{3,4,5,6};
        return vector<int>{};
    };
    auto hasFullPossible  = [&](){
        for(int v=1; v<=6; ++v) if (C[v]==5) return true;
        bool tri=false, pai=false;
        for(int v=1; v<=6; ++v){ tri |= (C[v]>=3); pai |= (C[v]>=2); }
        return tri && pai;
    };
    bool comboAngle = (!selectLarge().empty() || !selectSmall().empty() || hasFullPossible());

    // === 상황 기반 스마트 보충 ===
    // 하단 전부 사용됐는지 체크
auto allLowerUsed = [&](){
    for (int r = CHOICE; r <= YACHT; ++r)
        if (myState.ruleScore[r] == -1) return false;
    return true;
};

// 가중치 기반 보충: base 를 5개가 될 때까지, 얼굴별 weight가 낮은 것부터 채움
auto fillToFiveSmart = [&](vector<int> base, DiceRule target){
    array<int,7> tmp = C;
    for (int d: base) { if(!(1<=d&&d<=6) || tmp[d]==0) return vector<int>{}; tmp[d]--; }

    auto upperUsed = [&](int f){ return myState.ruleScore[f-1] != -1; };

    // 하단 전부 사용된 상태면: 이미 쓴 상단 얼굴 -> 남은 상단(작은눈부터) 순서로 채움
    if (allLowerUsed()){
        for (int f=6; f>=1 && (int)base.size()<5; --f)
            if (upperUsed(f)) while(tmp[f]>0 && (int)base.size()<5){ base.push_back(f); tmp[f]--; }
        for (int f=1; f<=6 && (int)base.size()<5; ++f)
            if (!upperUsed(f)) while(tmp[f]>0 && (int)base.size()<5){ base.push_back(f); tmp[f]--; }
        return base;
    }

    // 상황 플래그
    int rulesLeft=0; for (int r=0;r<12;++r) if (myState.ruleScore[r]==-1) ++rulesLeft;
    bool straightAlive = (myState.ruleScore[SMALL_STRAIGHT]==-1) || (myState.ruleScore[LARGE_STRAIGHT]==-1);
    bool midLate       = (roundNo >= 7 || rulesLeft <= 6);
    bool needOne       = (myState.ruleScore[ONE]==-1);
    bool needTwo       = (myState.ruleScore[TWO]==-1);
    bool needThree     = (myState.ruleScore[THREE]==-1);
    bool needFour      = (myState.ruleScore[FOUR]==-1);
    bool needFive      = (myState.ruleScore[FIVE]==-1);
    bool needSix       = (myState.ruleScore[SIX]==-1);

    // 상단 보너스 추적(고눈 보호용)
    auto remainingUpperFaces = [&](){
        vector<int> faces;
        for (int f=1; f<=6; ++f) if (myState.ruleScore[f-1]==-1) faces.push_back(f);
        return faces;
    };
    auto potentialUpper = [&](const array<int,7>& cnt, const vector<int>& faces){
        int pot = 0;
        for (int f: faces) pot += min(cnt[f],5) * f * 1000;
        return pot;
    };
    int currUpper=0; for (int i=0;i<6;++i) if (myState.ruleScore[i]!=-1) currUpper+=myState.ruleScore[i];
    vector<int> facesLeft = remainingUpperFaces();
    bool bonusTrackLikely = (currUpper + potentialUpper(tmp, facesLeft) >= 63000);

    // 얼굴별 가중치 계산 (작을수록 먼저 소비)
    auto faceCost = [&](int f)->int{
        if (tmp[f]==0) return INT_MAX/2;

        // 이미 상단에 쓴 얼굴은 '가치 없음' → 가장 먼저 태움
        if (upperUsed(f)) return -10000 - f; // 타이 브레이크로 낮은 눈 먼저

        int cost = 0;

        // 기본: 고눈일수록 보호(상대적으로 덜 쓰기)
        cost += f * 120;            // pip 보호

        // 스트레이트 살아있으면 2~5 보호
        if (straightAlive && 2<=f && f<=5) cost += 1400;

        // 4kind/풀하우스/야츠 각 보호
        if (tmp[f] + 0 /*이미 base에서 빠짐*/ + (C[f]-tmp[f]) >= 4 && myState.ruleScore[FOUR_OF_A_KIND]==-1)
            cost += 3500;           // 4kind 가능성
        if (C[f] >= 3 && myState.ruleScore[f-1]==-1) 
            cost += 2500;           // 상단 트리플 보호(초과 사용 억제)
        if (C[f] >= 5 && myState.ruleScore[YACHT]==-1) 
            cost += 6000;           // 야츠 보호

        // 중·후반: ONE/TWO/THREE 미사용이면 저눈 보호 조금 완화(= 덤프는 FOUR~SIX에 우선)
        if (midLate){
            if (f==1 && needOne)   cost += 2000; // ONE은 중후반에만 보호
            if (f==2 && needTwo)   cost += 800;
            if (f==3 && needThree) cost += 600;
        }

        // 보너스 트랙이 가능하면 4~6 더 보호
        if (bonusTrackLikely && (f>=4)) cost += 900;

        // 타깃 규칙별 미세 조정
        if (target==SMALL_STRAIGHT){
            // 작은 눈으로 채우는 경향
            cost += f * 15;
        }else if (target==CHOICE || target==FOUR_OF_A_KIND){
            // 점수↑ 유지: 고눈 보존 → 저눈 우선으로 태움
            cost += (7-f) * 10;
        }else if (ONE<=target && target<=SIX){
            // 상단에 두는 중이면 그 얼굴(f==face)은 이미 base에 들어가 있음.
            // 보충은 작은 눈 우선(단, 보호 규칙은 그대로 반영)
            cost += f * 20;
        }

        return cost;
    };

    // 가중치가 낮은 순으로 채움
    while ((int)base.size() < 5){
        int bestF = -1, bestCost = INT_MAX;
        for (int f=1; f<=6; ++f){
            int c = faceCost(f);
            if (c < bestCost){ bestCost = c; bestF = f; }
        }
        if (bestF==-1 || bestCost>=INT_MAX/2) break; // 더 넣을 수 없음(안전장치)
        base.push_back(bestF);
        tmp[bestF]--;
    }

    if ((int)base.size()!=5) return vector<int>{};
    return base;
};




    auto pickTop5_from_cnt = [&](){
        vector<int> out; out.reserve(5);
        for (int v = 6; v >= 1 && (int)out.size() < 5; --v) {
            int take = min(C[v], 5 - (int)out.size());
            for (int t = 0; t < take; ++t) out.push_back(v);
        }
        return out;
    };

    auto selectYacht = [&](){
        for (int v=1; v<=6; ++v) if (C[v] >= 5) return build_multi(v,5);
        return vector<int>{};
    };
    auto selectFourKind = [&](){ // 킥커 = 가장 큰 눈
        for (int v = 6; v >= 1; --v) if (C[v] >= 4) {
            int extra=-1; 
            for (int x=6; x>=1; --x) if (x!=v && C[x]>0){ extra=x; break; }
            if (extra==-1) extra=v;
            return vector<int>{v,v,v,v,extra};
        }
        return vector<int>{};
    };
    auto selectFullHouse = [&](){
        for (int v=1; v<=6; ++v) if (C[v]==5) return build_multi(v,5);
        int tri=-1, pai=-1;
        for (int v=6; v>=1; --v) if (C[v]>=3){ tri=v; break; }
        for (int v=6; v>=1; --v) if (v!=tri && C[v]>=2){ pai=v; break; }
        if (tri!=-1 && pai!=-1) return vector<int>{tri,tri,tri,pai,pai};
        return vector<int>{};
    };
    auto selectUpper  = [&](int face){
        int use = min(5, C[face]);
        return vector<int>(use, face);
    };
    auto ruleUsable = [&](DiceRule r){ return myState.ruleScore[r]==-1; };

    struct Cand { DiceRule r; vector<int> use; int raw; int eff; };
    vector<Cand> cs; cs.reserve(24);

    

    // YACHT(5/6)은 상단 우선: 상단 칸 비어있으면 YACHT 후보 생성 안 함
    bool preferUpperOverYacht =
        (C[6] >= 5 && ruleUsable(SIX)) ||
        (C[5] >= 5 && ruleUsable(FIVE));

    auto pushIf = [&](DiceRule r, vector<int> d){
        if (!ruleUsable(r)) return;
        if (r == YACHT && preferUpperOverYacht) return; // 컷
        // 상황 기반 스마트 보충
        d = fillToFiveSmart(d, r);
        if (d.empty()) return;
        int sc = GameState::calculateScore(r, d);
        cs.push_back({r, d, sc, sc});
    };

    // 후보 생성
    pushIf(YACHT,           selectYacht());
    pushIf(LARGE_STRAIGHT,  selectLarge());
    pushIf(FOUR_OF_A_KIND,  selectFourKind());
    pushIf(FULL_HOUSE,      selectFullHouse());
    pushIf(SMALL_STRAIGHT,  selectSmall());
    pushIf(CHOICE,          {});                     // base 없이 스마트 채움
    for (int face=6; face>=1; --face)
        pushIf((DiceRule)(face-1), selectUpper(face));
    // === (STRONG RULE) 트리플이 있으면 상단 먼저, 그 트리플로 만든 FULL_HOUSE는 사실상 금지 ===
    const int BOOST_TRIPLE_TO_UPPER   =  7000;    // 상단 후보 보너스(보수적/가점)
    const int KILL_FH_IF_TRIPLE_OPEN  = 100000;   // 트리플 상단이 비어있으면 풀하우스 사실상 금지
    const int EXTRA_KILL_IF_USE_2_5   =  10000;   // 2~5(스트레이트 핵심)까지 소모하면 추가 패널티

    bool smallAlive = (myState.ruleScore[SMALL_STRAIGHT]==-1);
    bool largeAlive = (myState.ruleScore[LARGE_STRAIGHT]==-1);
    bool straightAlive = smallAlive || largeAlive;

    

    for (auto &c : cs) {
        // 1) 상단 후보가 '그 얼굴'을 3개 이상 쓰면 살짝 가점 (정렬 안정화용)
        if (c.r >= ONE && c.r <= SIX) {
            int face = c.r + 1; // ONE=0 → face=1
            int usedCnt = 0;
            for (int d : c.use) if (d == face) ++usedCnt;
            if (usedCnt >= 3 && myState.ruleScore[c.r] == -1) {
                c.eff += BOOST_TRIPLE_TO_UPPER;
            }
        }
    }

    // 2) 풀하우스가 '트리플로 쓰인 얼굴'의 상단 칸이 비어 있으면 강하게 컷
    int rulesLeftLate = 0; for (int r=0; r<12; ++r) if (myState.ruleScore[r]==-1) ++rulesLeftLate;
    bool late9 = (rulesLeftLate <= 3);

    // 2) 풀하우스 강한 금지(초중반만), 후반엔 완화
    for (auto &c : cs) {
        if (c.r != FULL_HOUSE) continue;

        std::array<int,7> used{}; for (int d : c.use) if (1<=d && d<=6) used[d]++;
        int triFace = -1; for (int f=1; f<=6; ++f) if (used[f] >= 3) { triFace = f; break; }
        if (triFace == -1) continue;

        if (!late9) { // 초중반: 강하게 컷
            const int KILL_FH_IF_TRIPLE_OPEN = 100000;
            const int EXTRA_KILL_IF_USE_2_5  = 10000;
            if (myState.ruleScore[triFace-1] == -1) {
                c.eff -= KILL_FH_IF_TRIPLE_OPEN;
                bool straightAlive = (myState.ruleScore[SMALL_STRAIGHT]==-1 || myState.ruleScore[LARGE_STRAIGHT]==-1);
                if (straightAlive) {
                    int critUse = used[2] + used[3] + used[4] + used[5];
                    if (critUse > 0) c.eff -= EXTRA_KILL_IF_USE_2_5;
                }
            }
        } else {
            // 후반(라운드 9+): 금지 해제, 약한 페널티만 남김
            const int SOFT_PENALTY_LATE = 3000;
            if (myState.ruleScore[triFace-1] == -1) c.eff -= SOFT_PENALTY_LATE;
        }
    }

    if (cs.empty()) {
        auto any5 = pickTop5_from_cnt();
        auto d = fillToFiveSmart(any5, CHOICE);
        return DicePut{ (DiceRule)usable[0], d.empty()?any5:d };
    }

    // === (A) 저합계일 때 CHOICE/4K 약화 (상단 유도) ===
    bool hasUpper3 = false;
    for (int f=1; f<=6; ++f)
        if (ruleUsable((DiceRule)(f-1)) && C[f] >= 3) { hasUpper3 = true; break; }
    for (auto &c : cs){
        int sum = accumulate(c.use.begin(), c.use.end(), 0);
        if ((c.r == CHOICE || c.r == FOUR_OF_A_KIND) && hasUpper3 && sum <= 20){
            c.eff -= 5000;
        }
    }

    // === (B) 상단 보너스 EV 가중치 ===
    const int BONUS_NOW   = 35000;
    const int BONUS_TRACK = 14000;
    const int BONUS_DROP  = -7000;

    int currUpper = 0;
    for (int i=0; i<6; ++i) if (myState.ruleScore[i] != -1) currUpper += myState.ruleScore[i];

    auto remainingUpperFaces = [&](DiceRule r){
        vector<int> faces;
        for (int f=1; f<=6; ++f){
            bool used = (myState.ruleScore[f-1] != -1);
            bool thisFace = (ONE<=r && r<=SIX && (int)r == f-1);
            if (!used && !thisFace) faces.push_back(f);
        }
        return faces;
    };
    auto potentialUpper = [&](const array<int,7>& cnt, const vector<int>& faces){
        int pot = 0;
        for (int f: faces){
            int take = min(cnt[f], 5);
            pot += take * f * 1000;
        }
        return pot;
    };
        // === (C3) 보너스 포기 모드: 상단은 저눈(ONE 먼저)으로 덤프 ===
    // 현재 보유 cnt로 남은 상단 칸을 채워도 63k를 못 넘기면 덤프 모드
    auto upperFacesLeft = [&](){
        vector<int> faces;
        for (int f=1; f<=6; ++f) if (myState.ruleScore[f-1] == -1) faces.push_back(f);
        return faces;
    };
    int currUpper2 = currUpper;
    int maxFutureFromCnt = 0;
    for (int f: upperFacesLeft()) maxFutureFromCnt += min(C[f], 5) * f * 1000;
    bool dumpModeGlobal = (currUpper2 + maxFutureFromCnt < 63000);

    // 라운드가 거의 끝나가면(남은 칸 적음) 덤프 성향을 더 강화
    int rulesLeft = 0; for (int r=0; r<12; ++r) if (myState.ruleScore[r]==-1) ++rulesLeft;
    bool lateGame = (rulesLeft <= 2);

    if (dumpModeGlobal || lateGame) {
        const int DUMP_FACE_PENALTY_PER_PIP = 3000; // 얼굴값이 클수록 감점(고눈 아끼기)
        const int DUMP_ONE_EXTRA_BONUS      = 6000; // ONE에 버리기 가산
        for (auto &c : cs) {
            if (c.r >= ONE && c.r <= SIX) {
                int face = c.r + 1;
                int usedCnt = 0; for (int d : c.use) if (d == face) ++usedCnt;

                // 트리플 이상으로 상단을 꽉 채우는 건 그대로 두고,
                // 1~2개 같은 '애매한 상단'만 저눈 우선으로 강하게 유도
                if (usedCnt < 3) {
                    c.eff -= face * DUMP_FACE_PENALTY_PER_PIP;
                    if (face == 1) c.eff += DUMP_ONE_EXTRA_BONUS;
                }
            }
        }
    }
    int lateTier = (rulesLeft <= 2 ? 3 : rulesLeft <= 3 ? 2 : rulesLeft <= 4 ? 1 : 0);
    if (lateTier > 0) {
        // 단계별 부스트 (필요시 조절)
        const int BOOST4[4] = { 0, 7000, 11000, 15000 };  // 4kind
        const int BOOSTF[4] = { 0, 6000,  9000, 12000 };  // Full House

        for (auto &c : cs) {
            if (c.r == FOUR_OF_A_KIND && myState.ruleScore[FOUR_OF_A_KIND]==-1) {
                if (c.raw >= 14000) c.eff += BOOST4[lateTier];  // 너무 허접한 4kind는 제외
            }
            if (c.r == FULL_HOUSE && myState.ruleScore[FULL_HOUSE]==-1) {
                if (c.raw >= 15000) c.eff += BOOSTF[lateTier];  // 16k 같은 저득점은 과도 승격 방지
            }
        }
    }



    for (auto &c : cs){
        array<int,7> tmpCnt = C;
        for (int d: c.use) if (1<=d && d<=6) tmpCnt[d]--;

        int newUpper = currUpper + ((c.r>=ONE && c.r<=SIX) ? c.raw : 0);
        auto faces   = remainingUpperFaces(c.r);
        int pot      = potentialUpper(tmpCnt, faces);

        if (newUpper >= 63000) c.eff += BONUS_NOW;
        else if (newUpper + pot >= 63000) c.eff += BONUS_TRACK;
        else if (currUpper + pot < 63000) c.eff += BONUS_DROP;
    }

    if (rulesLeft==2 && totalDice>=10) {
        auto p = planFinalTwoExhaustive();
        plannedTwo.clear();
        plannedTwo.push_back(p.first);
        plannedTwo.push_back(p.second);
        auto out = plannedTwo.front(); plannedTwo.pop_front(); 
        return out;
    }

    auto rankRule = [&](DiceRule r)->int{
        static map<DiceRule,int> rank = {
            {YACHT,100},{LARGE_STRAIGHT,95},{FOUR_OF_A_KIND,90},
            {FULL_HOUSE,85},{CHOICE,80},{SMALL_STRAIGHT,75},
            {SIX,20},{FIVE,19},{FOUR,18},{THREE,17},{TWO,16},{ONE,15}
        };
        return rank[r];
    };

    // === 초반(라운드 1~5) 보존 전략 가중치 ===
auto usedCount = [&](const vector<int>& v, int face){
    int c=0; for (int d: v) if (d==face) ++c; return c;
};
auto usedCountSet = [&](const vector<int>& v, std::initializer_list<int> faces){
    int c=0; for (int d: v){ for (int f: faces){ if (d==f){ ++c; break; } } } return c;
};

bool early = (roundNo <= 5);
bool oneStraightLeft =
    (myState.ruleScore[SMALL_STRAIGHT]==-1) ^ (myState.ruleScore[LARGE_STRAIGHT]==-1);

if (early){
    // 1) 6/5 소모 패널티 (상단/선택/포카드에서 특히 강하게)
    for (auto &c : cs){
        bool penalizable = (c.r==CHOICE || c.r==FOUR_OF_A_KIND || (ONE<=c.r && c.r<=SIX));
        if (!penalizable) continue;
        int u6 = usedCount(c.use, 6);
        int u5 = usedCount(c.use, 5);
        c.eff -= u6 * 4000;
        c.eff -= u5 * 2500;
    }
    // 2) 직선 하나 남았으면 2~5는 보존(직접 스트레이트 점수 내는 경우 제외)
    if (oneStraightLeft){
        for (auto &c : cs){
            if (c.r==SMALL_STRAIGHT || c.r==LARGE_STRAIGHT) continue;
            int uS = usedCountSet(c.use, {2,3,4,5});
            c.eff -= uS * 1200;
        }
    }
    // 3) 낮은 상단 쿼드(특히 TWO) 보너스: “2 네 개면 TWO 우선”
    for (auto &c : cs){
        if (ONE<=c.r && c.r<=SIX){
            int face = c.r+1;
            if (face<=3 && C[face]>=4) c.eff += 12000 + 1000*(C[face]-3);
        }
    }
    // 4) 특례: 6이 3개↑ + 2가 4개↑ + TWO 미사용이면 → TWO 크게 밀어주고 SIX는 더 깎기
    if (C[6]>=3 && C[2]>=4 && myState.ruleScore[TWO]==-1){
        for (auto &c : cs){
            if (c.r==TWO) c.eff += 20000;
            if (c.r==SIX){ int u6 = usedCount(c.use,6); c.eff -= 6000*u6; }
        }
    }
}

    sort(cs.begin(), cs.end(), [&](const Cand& a, const Cand& b){
        if (a.eff != b.eff) return a.eff > b.eff;
        int ra = rankRule(a.r), rb = rankRule(b.r);
        if (ra != rb) return ra > rb;
        int sa = accumulate(a.use.begin(), a.use.end(), 0);
        int sb = accumulate(b.use.begin(), b.use.end(), 0);
        if (sa != sb) return sa < sb;
        return (int)a.use.size() > (int)b.use.size();
    });

    return DicePut{ cs[0].r, cs[0].use };
}

    // 상태 업데이트들 (원본과 동일)
    void updateGet(vector<int> diceA, vector<int> diceB, Bid myBid, Bid oppBid, char myGroup) {
        if (myGroup == 'A')
            myState.addDice(diceA), oppState.addDice(diceB);
        else
            myState.addDice(diceB), oppState.addDice(diceA);

        bool myBidOk = myBid.group == myGroup;
        myState.bid(myBidOk, myBid.amount);

        char oppGroup = myGroup == 'A' ? 'B' : 'A';
        bool oppBidOk = oppBid.group == oppGroup;
        oppState.bid(oppBidOk, oppBid.amount);

        myBidHist.push_back(myBid.amount);
        oppBidHist.push_back(oppBid.amount);
    }

    void updatePut(const DicePut& put) { myState.useDice(put); }
    void updateSet(const DicePut& put) { oppState.useDice(put); }
};

// 현재까지 획득한 총 점수 계산 (상단/하단 점수 + 보너스 + 입찰 점수)
int GameState::getTotalScore() const {
    int basic = 0, combination = 0, bonus = 0;
    for (int i = 0; i < 6; i++)
        if (ruleScore[i] != -1) basic += ruleScore[i];
    if (basic >= 63000) bonus += 35000; // 규칙 정의에 따름
    for (int i = 6; i < 12; i++)
        if (ruleScore[i] != -1) combination += ruleScore[i];
    return basic + bonus + combination + bidScore;
}

// 입찰 결과에 따른 점수 반영
void GameState::bid(bool isSuccessful, int amount) {
    if (isSuccessful) bidScore -= amount;  // 성공시 차감(지불)
    else              bidScore += amount;  // 실패시 획득
}

// 새로운 주사위들을 보유 목록에 추가
void GameState::addDice(const vector<int>& newDice) {
    dice.reserve(dice.size() + newDice.size());
    for (int d : newDice) {
        dice.push_back(d);                // 출력용으로만 유지
        if (1 <= d && d <= 6) cnt[d]++;   // 실제 소비는 cnt 기준
    }
}

// 주사위를 사용하여 특정 규칙에 배치(한번 사용한 건 다시 못 씀: cnt 감소)
void GameState::useDice(const DicePut& put) {
    assert(ruleScore[put.rule] == -1 && "Rule already used");
    for (int d : put.dice) {
        assert(1 <= d && d <= 6 && cnt[d] > 0 && "Invalid dice usage");
        cnt[d]--;
    }
    ruleScore[put.rule] = calculateScore(put.rule, put.dice);
}

// 주어진 규칙과 주사위에 대한 점수 계산 (복사 제거: const ref)
int GameState::calculateScore(DiceRule rule, const vector<int>& dice) {
    switch (rule) {
        case ONE:   return (int)count(dice.begin(), dice.end(), 1) * 1 * 1000;
        case TWO:   return (int)count(dice.begin(), dice.end(), 2) * 2 * 1000;
        case THREE: return (int)count(dice.begin(), dice.end(), 3) * 3 * 1000;
        case FOUR:  return (int)count(dice.begin(), dice.end(), 4) * 4 * 1000;
        case FIVE:  return (int)count(dice.begin(), dice.end(), 5) * 5 * 1000;
        case SIX:   return (int)count(dice.begin(), dice.end(), 6) * 6 * 1000;

        case CHOICE:
            return accumulate(dice.begin(), dice.end(), 0) * 1000;

        case FOUR_OF_A_KIND: {
            array<int,7> cnt{}; for (int d: dice) if (1<=d&&d<=6) cnt[d]++;
            bool ok = false;
            for (int i = 1; i <= 6; i++) if (cnt[i] >= 4) { ok = true; break; }
            return ok ? accumulate(dice.begin(), dice.end(), 0) * 1000 : 0;
        }
        case FULL_HOUSE: {
            array<int,7> cnt{}; for (int d: dice) if (1<=d&&d<=6) cnt[d]++;
            bool pair = false, triple = false;
            for (int i = 1; i <= 6; i++) {
                int c = cnt[i];
                if (c == 2 || c == 5) pair = true;
                if (c == 3 || c == 5) triple = true;
            }
            return (pair && triple) ? accumulate(dice.begin(), dice.end(), 0) * 1000 : 0;
        }
        case SMALL_STRAIGHT: {
            bool e1 = count(dice.begin(), dice.end(), 1) > 0;
            bool e2 = count(dice.begin(), dice.end(), 2) > 0;
            bool e3 = count(dice.begin(), dice.end(), 3) > 0;
            bool e4 = count(dice.begin(), dice.end(), 4) > 0;
            bool e5 = count(dice.begin(), dice.end(), 5) > 0;
            bool e6 = count(dice.begin(), dice.end(), 6) > 0;
            bool ok = (e1 && e2 && e3 && e4) || (e2 && e3 && e4 && e5) || (e3 && e4 && e5 && e6);
            return ok ? 15000 : 0;
        }
        case LARGE_STRAIGHT: {
            bool e1 = count(dice.begin(), dice.end(), 1) > 0;
            bool e2 = count(dice.begin(), dice.end(), 2) > 0;
            bool e3 = count(dice.begin(), dice.end(), 3) > 0;
            bool e4 = count(dice.begin(), dice.end(), 4) > 0;
            bool e5 = count(dice.begin(), dice.end(), 5) > 0;
            bool e6 = count(dice.begin(), dice.end(), 6) > 0;
            bool ok = (e1 && e2 && e3 && e4 && e5) || (e2 && e3 && e4 && e5 && e6);
            return ok ? 30000 : 0;
        }
        case YACHT: {
            array<int,7> cnt{}; for (int d: dice) if (1<=d&&d<=6) cnt[d]++;
            for (int i = 1; i <= 6; i++) if (cnt[i] == 5) return 50000;
            return 0;
        }
    }
    assert(false);
    return 0;
}
// 두수 완전탐색
pair<DicePut,DicePut> Game::planFinalTwoExhaustive() const {
    const auto &C0 = myState.cnt;

    // usable rules 목록
    vector<int> rules;
    for (int r=0; r<12; ++r) if (myState.ruleScore[r]==-1) rules.push_back(r);

    // 5개 손(h1,h2) 개수조합 생성
    vector<array<int,7>> hands1;
    array<int,7> cur{}; // 1..6 사용
    function<void(int,int)> gen1 = [&](int f, int rem){
        if (f==7){ if(rem==0) hands1.push_back(cur); return; }
        int cap = min(rem, C0[f]);
        for (int k=0;k<=cap;++k){ cur[f]=k; gen1(f+1, rem-k); }
        cur[f]=0;
    };
    gen1(1,5);

    auto expand = [&](const array<int,7>& h){
        vector<int> d; d.reserve(5);
        for (int f=1; f<=6; ++f) for (int k=0;k<h[f]; ++k) d.push_back(f);
        return d;
    };
    auto minusCnt = [&](array<int,7> a, const array<int,7>& b){
        for (int f=1; f<=6; ++f) a[f]-=b[f]; return a;
    };
    auto isUpper = [](DiceRule r){ return ONE<=r && r<=SIX; };

    int currUpper=0; for(int i=0;i<6;++i) if(myState.ruleScore[i]!=-1) currUpper+=myState.ruleScore[i];

    long long best = LLONG_MIN;
    DicePut best1{ONE,{}}, best2{ONE,{}};

    for (const auto& h1 : hands1){
        vector<int> d1 = expand(h1);

        // 남은 카운트로 h2 후보 전부
        array<int,7> C1 = minusCnt(C0, h1);
        vector<array<int,7>> hands2;
        array<int,7> cur2{};
        function<void(int,int)> gen2 = [&](int f, int rem){
            if (f==7){ if(rem==0) hands2.push_back(cur2); return; }
            int cap = min(rem, C1[f]);
            for (int k=0;k<=cap;++k){ cur2[f]=k; gen2(f+1, rem-k); }
            cur2[f]=0;
        };
        gen2(1,5);

        for (int r1 : rules){
            int s1 = GameState::calculateScore((DiceRule)r1, d1);
            for (const auto& h2 : hands2){
                vector<int> d2 = expand(h2);
                for (int r2 : rules){
                    if (r2==r1) continue; // 같은 규칙 중복 금지
                    int s2 = GameState::calculateScore((DiceRule)r2, d2);

                    // 상단 보너스(한 번만)
                    int addUpper = (isUpper((DiceRule)r1)?s1:0) + (isUpper((DiceRule)r2)?s2:0);
                    long long bonus = (currUpper < 63000 && currUpper + addUpper >= 63000) ? 35000 : 0;

                    long long tot = (long long)s1 + s2 + bonus;
                    if (tot > best){
                        best = tot;
                        best1 = DicePut{ (DiceRule)r1, d1 };
                        best2 = DicePut{ (DiceRule)r2, d2 };
                    }
                }
            }
        }
    }

    // 보기 좋게 1수 점수가 큰 걸 먼저 두기
    if (GameState::calculateScore(best2.rule, best2.dice) > 
        GameState::calculateScore(best1.rule, best1.dice)) std::swap(best1,best2);

    return {best1, best2};
}

// 입출력을 위해 규칙 enum을 문자열로 변환
string toString(DiceRule rule) {
    switch (rule) {
        case ONE: return "ONE";
        case TWO: return "TWO";
        case THREE: return "THREE";
        case FOUR: return "FOUR";
        case FIVE: return "FIVE";
        case SIX: return "SIX";
        case CHOICE: return "CHOICE";
        case FOUR_OF_A_KIND: return "FOUR_OF_A_KIND";
        case FULL_HOUSE: return "FULL_HOUSE";
        case SMALL_STRAIGHT: return "SMALL_STRAIGHT";
        case LARGE_STRAIGHT: return "LARGE_STRAIGHT";
        case YACHT: return "YACHT";
    }
    assert(!"Invalid Dice Rule");
    return "";
}

DiceRule fromString(const string& s) {
    if (s == "ONE") return ONE;
    if (s == "TWO") return TWO;
    if (s == "THREE") return THREE;
    if (s == "FOUR") return FOUR;
    if (s == "FIVE") return FIVE;
    if (s == "SIX") return SIX;
    if (s == "CHOICE") return CHOICE;
    if (s == "FOUR_OF_A_KIND") return FOUR_OF_A_KIND;
    if (s == "FULL_HOUSE") return FULL_HOUSE;
    if (s == "SMALL_STRAIGHT") return SMALL_STRAIGHT;
    if (s == "LARGE_STRAIGHT") return LARGE_STRAIGHT;
    if (s == "YACHT") return YACHT;
    assert(!"Invalid Dice Rule");
    return ONE;
}

// 표준 입력을 통해 명령어를 처리하는 메인 함수 (ROLL 파싱 유지)
int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    Game game;

    vector<int> diceA, diceB;
    Bid myBid{'A', 0};

    string line;
    while (true) {
        if (!std::getline(cin, line)) break;
        if (line.empty()) continue;

        istringstream iss(line);
        string command;
        if (!(iss >> command)) continue;

        if (command == "READY") {
            cout << "OK"<<endl;
            continue;
        }

        if (command == "ROLL") {
            // ROLL 파싱은 건드리지 않음
            string strA, strB;
            iss >> strA >> strB;
            diceA.clear();
            diceB.clear();
            for (char c : strA) if ('0'<=c&&c<='9') diceA.push_back(c - '0');
            for (char c : strB) if ('0'<=c&&c<='9') diceB.push_back(c - '0');
            myBid = game.calculateBid(diceA, diceB);
            cout << "BID " << myBid.group << " " << myBid.amount << endl;
            continue;
        }

        if (command == "GET") {
            char getGroup, oppGroup;
            int oppScore;
            iss >> getGroup >> oppGroup >> oppScore;
            game.updateGet(diceA, diceB, myBid, Bid{oppGroup, oppScore}, getGroup);
            continue;
        }

        if (command == "SCORE") {
            DicePut put = game.calculatePut();
            game.updatePut(put);
            cout << "PUT " << toString(put.rule) << " ";
            for (int d : put.dice) cout << d;  // 항상 5개 출력
            cout << endl;
            continue;
        }

        if (command == "SET") {
            string rule, str;
            iss >> rule >> str;
            vector<int> dice;
            for (char c : str) if ('0'<=c&&c<='9') dice.push_back(c - '0');
            game.updateSet(DicePut{fromString(rule), dice});
            continue;
        }

        if (command == "FINISH") break;

        // 알 수 없는 명령어는 무시(로그만)
        cerr << "Invalid command: " << command << endl;
    }

    return 0;
}
