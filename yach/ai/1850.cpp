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

// 라운드 가중치
constexpr double LARGE_STRAIGHT_ROUND_RATE =0.12;
constexpr double SMALL_STRAIGHT_ROUND_RATE =0.12;

// 입찰 가중치
constexpr int LARGE_STRAIGHT_BID=60;
constexpr int SMALL_STRAIGHT_BID=40;
constexpr int ROUND_ONE_MAX_BAT = 3812;

constexpr int FULL_HOUSE_BID=20;
constexpr double FULL_HOUSE_ROUND_RATE =0.4;

constexpr int FOUR_KIND_WEIGHT = 20;
constexpr double FOUR_KIND_ROUND_RATE =0.4;
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
    char now;
    mutable vector<int> bestPutA, bestPutB;
    mutable int bestRuleA = -1, bestRuleB = -1;

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
    deque<DicePut> planned; 
    int roundNo = 0;
    int lastMyBid = 0;
    int lastOppBid = 0;
    long long oppBidSum = 0;
    int oppBidCount = 0;
    deque<int> myBidHist, oppBidHist;

    std::pair<int,int> getOppBidMinMax(bool excludeNearZero = false) const {
        if (oppBidHist.empty()) return {0, 0};

        int mn = INT_MAX, mx = INT_MIN;
        for (int b : oppBidHist) {
            if (excludeNearZero && std::abs(b) <= NEAR_ZERO_THRESH) continue;
            mn = std::min(mn, b);
            mx = std::max(mx, b);
        }
        if (mn == INT_MAX) return {0, 0}; // 전부 near-zero로 제외된 경우
        return {mn, mx};
    };
    double getOppBidAvg(int lastK = -1, bool excludeNearZero = false) const {
        if (oppBidHist.empty()) return 0.0;

        int n = 0;
        long long sum = 0;

        int start = 0;
        if (lastK > 0 && lastK < (int)oppBidHist.size())
            start = (int)oppBidHist.size() - lastK;

        for (int i = start; i < (int)oppBidHist.size(); ++i) {
            int b = oppBidHist[i];
            if (excludeNearZero && std::abs(b) <= NEAR_ZERO_THRESH) continue;
            sum += b; ++n;
        }
        return n ? (double)sum / n : 0.0;
    }
    
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

    auto selectLarge = [&](const array<int,7> &dice){
        int a[5]={1,2,3,4,5}, b[5]={2,3,4,5,6};
        auto ok=[&](int* s){ for(int i=0;i<5;i++) if(dice[s[i]]==0) return false; return true; };
        if (ok(a)) return vector<int>{1,2,3,4,5};
        if (ok(b)) return vector<int>{2,3,4,5,6};
        return vector<int>{};
    };
    auto selectSmall = [&](const array<int,7> &dice){
        int a[4]={1,2,3,4}, b[4]={2,3,4,5}, c[4]={3,4,5,6};
        auto ok=[&](int* s){ for(int i=0;i<4;i++) if(dice[s[i]]==0) return false; return true; };
        if (ok(a)) return vector<int>{1,2,3,4};
        if (ok(b)) return vector<int>{2,3,4,5};
        if (ok(c)) return vector<int>{3,4,5,6};
        return vector<int>{};
    };
    

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

    auto calprefer5 =[&](const vector<int>& dice, const GameState &myState)->double {
        array<int,7> curdice{};
        for(auto a:dice){
            curdice[a]++;
        }
        double dap=0;
            auto sumv = [](const vector<int>& v){ return accumulate(v.begin(), v.end(), 0); };
            auto cntNum = [](const vector<int>& v,int num){ return (int)count(v.begin(), v.end(), num); };
            if(myState.ruleScore[CHOICE]==-1){
                dap+=sumv(dice)*1.6+sumv(dice)*roundNo*0.1*1.3;
            }
            if(roundNo>=4){ // 6아끼기 전략임
                if(myState.ruleScore[SIX]==-1){
                    dap+=cntNum(dice,6)*17;
                }
                else{
                    dap+=cntNum(dice,6)*5;
                }
            }
            if(myState.ruleScore[LARGE_STRAIGHT]==-1){
                auto v=selectLarge(curdice);
                if(v.size()!=0)dap+=LARGE_STRAIGHT_BID+LARGE_STRAIGHT_BID*roundNo*LARGE_STRAIGHT_ROUND_RATE;
                auto v2=selectSmall(curdice);
                if(!v2.empty()){
                    dap+=13;
                }
            }
            if(myState.ruleScore[SMALL_STRAIGHT]==-1){
                auto v=selectSmall(curdice);
                if(!v.empty()){
                    dap+=SMALL_STRAIGHT_BID+SMALL_STRAIGHT_BID*roundNo*SMALL_STRAIGHT_ROUND_RATE;
                }
            }
            for(int i=0; i<6; i++){
                if(myState.ruleScore[i]==-1){
                    int cnt=curdice[i+1];
                    dap+=cnt*curdice[i+1];
                    if(cnt==3&&myState.ruleScore[YACHT]==-1){
                        dap+=pow(2,cnt)*(1+i)*0.8;
                        if(i==0){
                            dap+=5;
                        }
                    }
                    if(cnt>=4){
                        if(myState.ruleScore[YACHT]!=-1){
                            dap+=pow(2,cnt)*(1+i)*0.8;
                        }
                        else{
                            dap+=pow(3,cnt)*(1+i)*1.2;
                            if(1+i==1)
                            dap+=30;
                        }
                        if(myState.ruleScore[YACHT]==-1&&i==0){
                            dap+=21;
                        }
                        if(myState.ruleScore[YACHT]==-1&&roundNo<=10){
                            dap+=120;
                        }
                    }                     
                    if(cnt==5&&myState.ruleScore[YACHT]==-1){
                        dap+=171;
                    }
                    if(myState.ruleScore[YACHT]!=-1){
                        if(cnt==2&&(1+i)>=4){
                            dap+=pow(2,cnt)*(1+i)*0.8;
                        }
                    }                                                                                      
                }
            }
            return dap;
    };
    
    auto calprefer = [&](const vector<int>& dice,const GameState &myState,char who)->double {
        // for(auto a:dice){
        //     cout<<a<<" ";
        // }
        // cout<<"주사위 \n";
        array<int,7> curdice=myState.cnt;
        for(auto a:dice){
            curdice[a]++;
        }
        if(roundNo==1){
            return calprefer5(dice,myState);
        }
        else if(roundNo!=12){ //중반 라운드 *********************
            //curdice에 현재 사용가능한 10개의 주사위가 있음 
            // 이것을 사용하여 5개의 숫자와 뽑지 않은 나머지 5개는 calprefer5함수를 통해 적당히 더하여서 가장좋은 조합을 선택해서 myState에 vector변수 추가해서 숫자 5개 넣어줘야함
            // 중반 라운드: 10개 풀에서 최적 5개 + 남은 5개 가중
            // curdice == myState.cnt + dice 로 합쳐진 10개 풀(카운트)
            // 모든 5개 멀티셋 후보 열거
        std::vector<std::vector<int>> candidates;
        std::vector<int> cur;
        std::function<void(int, std::array<int,7>, int)> dfs =
            [&](int face, std::array<int,7> left, int taken){
                if (taken == 5) { candidates.push_back(cur); return; }
                if (face > 6) return;
                int can = std::min(left[face], 5 - taken);

                // 0개 선택
                dfs(face+1, left, taken);
                // 1..can개 선택
                for (int k=1; k<=can; ++k) {
                    cur.insert(cur.end(), k, face);
                    left[face] -= k;
                    dfs(face+1, left, taken + k);
                    left[face] += k;
                    cur.resize(cur.size() - k);
                }
            };
        dfs(1, curdice, 0);

        // pick을 쓰고 남는 5개 만들기
        auto buildRest = [&](const std::vector<int>& pick){
            std::array<int,7> cnt = curdice;
            for (int v: pick) { if (v<1||v>6 || cnt[v]==0) return std::vector<int>{}; cnt[v]--; }
            std::vector<int> rest; rest.reserve(5);
            for (int f=1; f<=6; ++f) for (int c=0; c<cnt[f]; ++c) rest.push_back(f);
            if ((int)rest.size() != 5) return std::vector<int>{};
            return rest;
        };

        // ===== 즉시 점수(=어디에 넣을지) 계산 =====
        auto bestImmediateRule = [&](const std::vector<int>& pick)->std::pair<int,double>{
            // ruleIndex, scoreNow
            std::array<int,7> pc{}; for (int v: pick) pc[v]++;

            auto isSmall = [&](){
                auto has=[&](std::initializer_list<int> s){ for(int t:s) if(pc[t]==0) return false; return true; };
                return has({1,2,3,4}) || has({2,3,4,5}) || has({3,4,5,6});
            };
            auto isLarge = [&](){
                auto has=[&](std::initializer_list<int> s){ for(int t:s) if(pc[t]==0) return false; return true; };
                return has({1,2,3,4,5}) || has({2,3,4,5,6});
            };
            auto isYacht = [&](){ for (int f=1; f<=6; ++f) if (pc[f]==5) return true; return false; };
            // 풀하우스/포카드가 룰로 있다면 필요 시 해제
            auto isFull = [&](){ bool tri=false,pair=false; for(int f=1;f<=6;++f){ tri|=(pc[f]==3); pair|=(pc[f]==2);} return tri&&pair; };
            auto isFour = [&](){ for(int f=1;f<=6;++f) if(pc[f]>=4) return true; return false; };

            int bestRule = -1;
            double bestScore = -1e100;

            // 상단 (ONES~SIXES: 0..5)
            for (int i=0; i<6; ++i) if (myState.ruleScore[i]==-1) {
                double sc = pc[i+1] * (i+1); // 즉시 점수
                if(i+1==1){
                    sc+=18;//30
                }
                if(i+1==2){
                    sc+=9; //20
                }
                if(pc[i+1]==3){
                    sc+=pow(2,pc[i+1])*(1+i)*0.8;
                    if(roundNo<=6){
                        if(i+1==6){
                            sc*=0.7;
                        }
                        if(i+1==5){
                            sc*=0.8;
                        }
                    }
                }
                if(pc[i+1]>=4&&myState.ruleScore[YACHT]!=-1){
                    sc+=pow(3,pc[i+1])*(1+i);
                    if(i+1<=3){
                        sc*=0.6;
                    }
                }
                if(pc[i+1]==4 &&myState.ruleScore[YACHT]==-1){
                    sc+=pow(2,pc[i+1])*(1+i)*0.8;
                    if(i+1<=3){
                        sc-=30;
                    }
                }
                if(pc[i+1]==5){
                    sc+=1000; /// **********
                    if(i+1<=3&& myState.ruleScore[YACHT]==-1){
                        sc-=1000;//////////********* 보너스 받을 수 있으면 로직이 바뀌어야하는거 추가로 넣어줘야함*/
                    }
                    if(i+1==4&& myState.ruleScore[YACHT]==-1){
                        sc-=500;
                    }
                    if(i+1>=5){
                        sc+=120;
                    }
                }
                if (sc > bestScore) { bestScore=sc; bestRule=i; }
            }



            // FULL HOUSE
            if (myState.ruleScore[FULL_HOUSE]==-1 && isFull()) {
                double s=0; for (int f=1; f<=6; ++f) s += f*pc[f];
                double sc = s*1.09 + FULL_HOUSE_BID * roundNo * FULL_HOUSE_ROUND_RATE;
                if(s>=23&&roundNo>=6){
                    sc+=40;
                }
                if(roundNo<=7&& s<20){
                    sc-=13;
                }
                if (sc > bestScore) { bestScore = sc; bestRule = FULL_HOUSE; }
            }

            // FOUR OF A KIND (합 기반)
            if (myState.ruleScore[FOUR_OF_A_KIND]==-1 && isFour()) {
                double s = 0; for (int f=1; f<=6; ++f) s += f * pc[f]; // pick의 합
                double sc = s + s * roundNo * FOUR_KIND_ROUND_RATE;
                if(s>=22){
                    sc+=40;
                }
                if(roundNo<=7&& s<20){
                    sc-=17;
                }
                if (sc > bestScore) { bestScore = sc; bestRule = FOUR_OF_A_KIND; }
            }



            // CHOICE (8)
            if (myState.ruleScore[CHOICE]==-1) {
                int s=0; for (int f=1; f<=6; ++f) s += f*pc[f];
                // ‘옛날 느낌’ 유지: 라운드 가중
                double sc = s*0.9 + s*roundNo*0.1*1.3;
                if(s<24){
                    sc-=100;
                }
                if(roundNo<=7){
                    sc-=50;
                }
                if (sc > bestScore) { bestScore=sc; bestRule=CHOICE; }
            }

            // SMALL STRAIGHT (9)
            if (myState.ruleScore[SMALL_STRAIGHT]==-1 && isSmall()) {
                double sc = SMALL_STRAIGHT_BID + SMALL_STRAIGHT_BID*roundNo*SMALL_STRAIGHT_ROUND_RATE;
                if (sc > bestScore) { bestScore=sc; bestRule=SMALL_STRAIGHT; }
            }

            // LARGE STRAIGHT (10)
            if (myState.ruleScore[LARGE_STRAIGHT]==-1 && isLarge()) {
                double sc = LARGE_STRAIGHT_BID + LARGE_STRAIGHT_BID*roundNo*LARGE_STRAIGHT_ROUND_RATE;
                if(roundNo<=6){
                    sc+=7;
                }
                if (sc > bestScore) { bestScore=sc; bestRule=LARGE_STRAIGHT; }
            }

            // YACHT (11) — calprefer5와 스케일 맞추려 171 사용
            if (myState.ruleScore[YACHT]==-1 && isYacht()) {
                double sc = 1001;
                if (sc > bestScore) { bestScore=sc; bestRule=YACHT; }
            }
            // 풀하우스/포카드 룰이 있다면 여기에 추가 (상수 인덱스 맞춰서)
            // if (myState.ruleScore[FULL_HOUSE]==-1 && isFull()) { double sc = FULL_HOUSE_BID; ... }
            // if (myState.ruleScore[FOUR_OF_A_KIND]==-1 && isFour()) { double sc = sum(pick) (혹은 가중); ... }
            
            return {bestRule, bestScore};
        };

        const double NOW_WEIGHT  = 3.0; // "먼저 쓸 것" 가중치 (더 큼)
        const double REST_WEIGHT = 0.5; // 남는 5개 가중치 (작게; 튜닝 가능)

        double bestTotal = -1e100;
        std::vector<int> bestPick, bestRest;
        int bestRuleIdx = -1;

        for (const auto& pick : candidates) {
            auto rest = buildRest(pick);
            if (rest.empty()) continue;

            // 어디에 넣을지 + 즉시점수
            auto [ruleIdx, nowScore] = bestImmediateRule(pick);
            if (ruleIdx < 0) continue; // 넣을 수 있는 칸이 없으면 패스

            // 그 칸을 사용한 가정으로 남은 5개 평가 (myState는 복사본으로 업데이트)
            GameState st2 = myState;
            st2.ruleScore[ruleIdx] = 0; // 사용 처리 (점수 값은 의미 없음, ‘사용됨’ 표시만)

            double restScore = calprefer5(rest, st2);

            double total = NOW_WEIGHT * nowScore + REST_WEIGHT * restScore;

            if (total > bestTotal) {
                bestTotal  = total;
                bestPick   = pick;
                bestRest   = rest;
                bestRuleIdx = ruleIdx;
            }
        }

        if (!bestPick.empty()) {
            // PUT 때 쓰도록 저장
            if (who=='A') { myState.bestPutA = bestPick; myState.bestRuleA = bestRuleIdx; }
            else          { myState.bestPutB = bestPick; myState.bestRuleB = bestRuleIdx; }
            return bestTotal;
        } else {
            // 방어: 후보가 없으면 현재 5개 그대로 + 어디 넣을지는 CHOICE or 상단 최대 등으로
            auto [ruleIdx, nowScore] = bestImmediateRule(dice);
            if (who=='A') { myState.bestPutA = dice; myState.bestRuleA = ruleIdx; }
            else          { myState.bestPutB = dice; myState.bestRuleB = ruleIdx; }
            return NOW_WEIGHT*nowScore; // rest 없음
        }
        }   
        else { // 마지막 라운드: 10개 풀에서 5개+5개 완전탐색, 합산 최대
        // 1) 이번 라운드에 실제로 쓸 10개 풀(이미 위에서 curdice에 myState.cnt + dice 합쳐둠)
        const std::array<int,7> C0 = curdice;

        // 2) 남은 규칙(사용 안 한 칸들)
        std::vector<int> rules;
        for (int r=0; r<12; ++r) if (myState.ruleScore[r]==-1) rules.push_back(r);
        if (rules.size() < 2) {
            // 방어: 한 칸만 남았거나 이상 상태면 그냥 현재 5개 평가로 대체
            if (who=='A') { myState.bestPutA = dice; myState.bestRuleA = (rules.empty()?CHOICE:rules[0]); }
            else          { myState.bestPutB = dice; myState.bestRuleB = (rules.empty()?CHOICE:rules[0]); }
            return calprefer5(dice, myState);
        }

        // 3) 유틸
        auto expand = [&](const std::array<int,7>& h){
            std::vector<int> d; d.reserve(5);
            for (int f=1; f<=6; ++f) for (int k=0;k<h[f]; ++k) d.push_back(f);
            return d;
        };
        auto minusCnt = [&](std::array<int,7> a, const std::array<int,7>& b){
            for (int f=1; f<=6; ++f) a[f]-=b[f]; return a;
        };
        auto isUpper = [](int r)->bool{ return ONE<=r && r<=SIX; };

        // 현재 상단 누적(보너스 계산용)
        int currUpper=0; for (int i=0;i<6;++i) if (myState.ruleScore[i]!=-1) currUpper+=myState.ruleScore[i];

        // 4) 5개 멀티셋 전부 생성 (용량 제약 포함)
        std::vector<std::array<int,7>> hands1;
        std::array<int,7> curH{}; // 1..6 사용
        std::function<void(int,int,const std::array<int,7>&)> gen1 =
            [&](int f, int rem, const std::array<int,7>& C){
                if (f==7){ if(rem==0) hands1.push_back(curH); return; }
                int cap = std::min(rem, C[f]);
                for (int k=0;k<=cap;++k){ curH[f]=k; gen1(f+1, rem-k, C); }
                curH[f]=0;
            };
        gen1(1,5,C0);

        long long bestTot = LLONG_MIN;
        std::vector<int> bestD1, bestD2;
        int bestR1=-1, bestR2=-1;

        for (const auto& h1 : hands1){
            // 남은 카운트로 h2 전부 생성
            std::array<int,7> C1 = minusCnt(C0, h1);

            std::vector<std::array<int,7>> hands2;
            std::array<int,7> cur2{};
            std::function<void(int,int,const std::array<int,7>&)> gen2 =
                [&](int f, int rem, const std::array<int,7>& C){
                    if (f==7){ if(rem==0) hands2.push_back(cur2); return; }
                    int cap = std::min(rem, C[f]);
                    for (int k=0;k<=cap;++k){ cur2[f]=k; gen2(f+1, rem-k, C); }
                    cur2[f]=0;
                };
            gen2(1,5,C1);

            std::vector<int> d1 = expand(h1);

            for (const auto& h2 : hands2){
                std::vector<int> d2 = expand(h2);

                // 두 칸에 규칙 할당(서로 다른 규칙)
                for (int r1 : rules){
                    int s1 = GameState::calculateScore((DiceRule)r1, d1);
                    for (int r2 : rules){
                        if (r2==r1) continue;
                        int s2 = GameState::calculateScore((DiceRule)r2, d2);

                        // 상단 보너스 (한 번만)
                        int addUpper = (isUpper(r1)?s1:0) + (isUpper(r2)?s2:0);
                        long long bonus = (currUpper < 63000 && currUpper + addUpper >= 63000) ? 35000 : 0;

                        long long tot = (long long)s1 + s2 + bonus;
                        if (tot > bestTot){
                            bestTot = tot;
                            bestD1 = d1; bestD2 = d2;
                            bestR1 = r1; bestR2 = r2;

                            // “지금 둘 수”를 첫 수로: 즉시 점수가 큰 걸 앞으로
                            if (s2 > s1){
                                std::swap(bestD1,bestD2);
                                std::swap(bestR1,bestR2);
                            }
                        }
                    }
                }
            }
        }

        // 결과 저장(= PUT 때 사용)
        if (who=='A'){ myState.bestPutA = bestD1; myState.bestRuleA = bestR1; }
        else         { myState.bestPutB = bestD1; myState.bestRuleB = bestR1; }

        // 입찰 평가값으로는 “합산 점수”를 반환 (요구사항)
        return static_cast<double>(bestTot);
    }

    };

    

    
    double preferdiceA=calprefer(diceA,myState,'A');
    double preferdiceB=calprefer(diceB,myState,'B');
    char bat = (preferdiceA > preferdiceB) ? 'A' : 'B';

    double preferdiceYoursA=calprefer(diceA,oppState,'A');
    double preferdiceYoursB=calprefer(diceB,oppState,'B');
    char yoursbat= (preferdiceYoursA > preferdiceYoursB) ? 'A' : 'B';

    double multiple=1;
    auto want01 = [](double a, double b){
        double s = a + b;
        return s > 0 ? std::abs(a-b)/s : 0.0;
    };
    double myW  = want01(preferdiceA,       preferdiceB);
    double oppW = want01(preferdiceYoursA,  preferdiceYoursB);
    double w    = 0.5*(myW + oppW);              // 둘의 평균 강도(0~1)
    double mult = (bat == yoursbat) ? (1.0 + 0.8*w) : (1.0 - 0.6*w);
    // cout<<preferdiceA<<" "<<preferdiceB<<"\n";
    double sum = preferdiceA + preferdiceB;
    double diffRatio = fabs(preferdiceA - preferdiceB) / sum; // 0 ~ 1
    double absLevel = std::max(preferdiceA, preferdiceB) / 100.0; // 점수 스케일 0~1 (100 기준)

    // 가중치: 차이 비율과 절대 수준을 곱해서 확신 정도 계산
    double confidence = diffRatio * absLevel;

    // Kelly 변형: 확신 정도를 그대로 비율로 사용
    double multiplier = 1.0 + confidence * 0.8+mult;
    // cout<<multiplier<<"\n";
    double multiplier2 = fabs(preferdiceA - preferdiceB) / sum; // 0 ~ 1
    // cout<<multiplier2<<"\n";
    array<int,7> cntA{}, cntB{};
    for (int x : diceA) cntA[x]++;
    for (int x : diceB) cntB[x]++;

    if(roundNo==1){
        int sixa=0, sixb=0;
        Bid one;
        int suma=0,sumb=0;
        for(auto temp:diceA)if(temp==6)sixa++;
        for(auto temp:diceB)if(temp==6)sixb++;
        if(sixa!=0||sixb!=0){// 6이 하나라도 있으면 그걸로 돌진
            if(sixa>sixb){
                int amount = INIT_BID_BY_SIX[sixa-sixb];
                if(sixa>=4) amount+=500;
                one.group='A';
                one.amount=amount;
            }
            else if(sixa==sixb){//원래대로하기
            }
            else{
                int amount = INIT_BID_BY_SIX[sixb-sixa];
                if(sixb>=4) amount+=500;
                one.group='B';
                one.amount=amount;
            }
            if(sixa!=sixb){
                return one;
            }
        }
    }



    if(cntA==cntB){
        return Bid{'A',0};
    }
    
    int amount=int(multiplier2*ROUND_ONE_MAX_BAT)+17;
    if(max(preferdiceA,preferdiceB)<50)amount-=732;
    if(abs(preferdiceA - preferdiceB)>153)amount+=1124;
    if(abs(preferdiceA - preferdiceB)>203)amount+=4124;
    if(roundNo!=12&&myState.ruleScore[YACHT]!=-1) amount=min(amount,4123);
    if(roundNo<=6) amount=min(amount,3132);
    

    auto [oppMinNZ, oppMaxNZ] = getOppBidMinMax(true);
    if(amount>oppMaxNZ) amount=min(amount,oppMaxNZ+1001);
    double oppAvgLast5 = getOppBidAvg(5);  
    if(oppAvgLast5<amount) amount-=200;
    if(amount<0)amount=17;
    return Bid{bat,amount};
}





    // ======== 배치 전략 (항상 5개 출력, cnt 기반으로 빠르게) ========

DicePut calculatePut() {
    // bestPutA, bestPutB;
    // mutable int bestRuleA = -1, bestRuleB = -1;

    vector<int> usable;
    for (int r = 0; r < 12; ++r) if (myState.ruleScore[r] == -1) usable.push_back(r);
    if(usable.size()==1){
        vector<int> dice;
        for(int i=1; i<=6; i++){
            for(int j=0; j<myState.cnt[i]; j++){
                dice.push_back(i);
            }
        }
        return DicePut{(DiceRule)usable[0], dice};
    }
        

    

    if(myState.now=='A'){
        return DicePut{ (DiceRule)myState.bestRuleA, myState.bestPutA };
    }
    else{
        return DicePut{ (DiceRule)myState.bestRuleB, myState.bestPutB };
    }
    if (!planned.empty()) { auto out = planned.front(); planned.pop_front(); return out;}
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
        planned.clear();
        planned.push_back(p.first);
        planned.push_back(p.second);
        auto out = planned.front(); planned.pop_front(); 
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
        if (myGroup == 'A'){
            myState.addDice(diceA), oppState.addDice(diceB);
            myState.now='A';
        }
        else{
            myState.addDice(diceB), oppState.addDice(diceA);
            myState.now='B';
        }
        
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
