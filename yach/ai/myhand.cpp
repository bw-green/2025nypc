#include <algorithm>
#include <cassert>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <map>

using namespace std;

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

    // ======== 입찰 전략 ========
    // ======== 입찰 전략: 즉시 가치 + 보너스 + 견제 기반 ========
Bid calculateBid(const vector<int>& diceA, const vector<int>& diceB) {
    auto evalBest = [&](const GameState& base, const vector<int>& add)->int {
        // base 상태에 add 주사위를 더했을 때 바로 낼 수 있는 최고 "가치" (raw + 보너스 즉시 달성 가산)
        array<int,7> C = base.cnt;
        for (int d : add) if (1<=d && d<=6) C[d]++;

        auto fillToFive = [&](vector<int> v){
            array<int,7> tmp=C;
            for(int x:v){ if(!(1<=x&&x<=6) || tmp[x]==0) return vector<int>{}; tmp[x]--; }
            for(int f=6; f>=1 && (int)v.size()<5; --f) while(tmp[f]>0 && (int)v.size()<5){ v.push_back(f); tmp[f]--; }
            return (int)v.size()==5 ? v : vector<int>{};
        };
        auto top5 = [&](){
            vector<int> v; v.reserve(5);
            for(int f=6; f>=1 && (int)v.size()<5; --f){
                int take=min(C[f], 5-(int)v.size());
                for(int i=0;i<take;i++) v.push_back(f);
            }
            return v;
        };
        auto selYacht = [&](){
            for(int f=1; f<=6; ++f) if(C[f]>=5) return vector<int>(5,f);
            return vector<int>{};
        };
        auto selLarge = [&](){
            int a[5]={1,2,3,4,5}, b[5]={2,3,4,5,6};
            auto ok=[&](int* s){ for(int i=0;i<5;i++) if(C[s[i]]==0) return false; return true; };
            if(ok(a)) return vector<int>{1,2,3,4,5};
            if(ok(b)) return vector<int>{2,3,4,5,6};
            return vector<int>{};
        };
        auto selSmall = [&](){
            int a[4]={1,2,3,4}, b[4]={2,3,4,5}, c[4]={3,4,5,6};
            auto ok=[&](int* s){ for(int i=0;i<4;i++) if(C[s[i]]==0) return false; return true; };
            if(ok(a)) return vector<int>{1,2,3,4};
            if(ok(b)) return vector<int>{2,3,4,5};
            if(ok(c)) return vector<int>{3,4,5,6};
            return vector<int>{};
        };
        auto selFour = [&](){
            for(int f=6; f>=1; --f) if(C[f]>=4){
                int kick=-1; for(int k=6;k>=1;--k) if(k!=f && C[k]>0){ kick=k; break; }
                if(kick==-1) kick=f;
                return vector<int>{f,f,f,f,kick};
            }
            return vector<int>{};
        };
        auto selFull = [&](){
            for(int f=1; f<=6; ++f) if(C[f]==5) return vector<int>(5,f);
            int tri=-1,pairv=-1;
            for(int f=6; f>=1; --f) if(C[f]>=3){ tri=f; break; }
            for(int f=6; f>=1; --f) if(f!=tri && C[f]>=2){ pairv=f; break; }
            if(tri!=-1 && pairv!=-1) return vector<int>{tri,tri,tri,pairv,pairv};
            return vector<int>{};
        };
        auto selUpper = [&](int face){
            int use=min(5,C[face]); vector<int> v(use, face);
            return v;
        };
        auto usable = [&](DiceRule r){ return base.ruleScore[r]==-1; };

        int best = 0; // 최고 가치
        auto consider = [&](DiceRule r, vector<int> v, bool needFill){
            if (!usable(r)) return;
            if (v.empty() && !(ONE<=r && r<=SIX)) return;
            if (needFill) v = fillToFive(v);
            if (v.empty()) return;
            int raw = GameState::calculateScore(r, v);

            // 이번 한 수로 상단 보너스 즉시 달성 시 +35k 가산
            int currUpper = 0;
            for (int i=0;i<6;i++) if (base.ruleScore[i]!=-1) currUpper += base.ruleScore[i];
            if (ONE<=r && r<=SIX && currUpper + raw >= 63000) raw += 35000;

            best = max(best, raw);
        };

        consider(YACHT,          selYacht(),        false);
        consider(LARGE_STRAIGHT, selLarge(),        false);
        consider(FOUR_OF_A_KIND, selFour(),         false);
        consider(FULL_HOUSE,     selFull(),         false);
        consider(SMALL_STRAIGHT, selSmall(),        true);
        consider(CHOICE,         top5(),            false);
        for(int f=6; f>=1; --f) consider((DiceRule)(f-1), selUpper(f), true);

        return best;
    };

    // 내 관점에서의 즉시 가치
    int vA = evalBest(myState, diceA);
    int vB = evalBest(myState, diceB);

    // 상대가 가져가게 되는 반대 그룹의 가치(견제 효과)
    int oppIfIA = evalBest(oppState, diceB); // 내가 A를 먹으면 상대는 B
    int oppIfIB = evalBest(oppState, diceA); // 내가 B를 먹으면 상대는 A
    int denyA = max(0, oppIfIB - oppIfIA);   // A를 택하면 상대가 잃는 가치
    int denyB = max(0, oppIfIA - oppIfIB);   // B를 택하면 상대가 잃는 가치

    // 선택: 내 가치 + 일부 견제효과가 큰 쪽
    int scoreA = vA + denyA/2;  // 견제는 절반만 반영(안정적)
    int scoreB = vB + denyB/2;
    char group = (scoreA >= scoreB ? 'A' : 'B');

    int myBest   = max(scoreA, scoreB);
    int myOther  = min(scoreA, scoreB);
    int margin   = max(0, myBest - myOther);          // 유효 격차(점수 단위)

    // 리드/라운드에 따른 공격성
    int lead = myState.getTotalScore() - oppState.getTotalScore();
    int rulesLeft = 0; for(int r=0;r<12;++r) if(myState.ruleScore[r]==-1) ++rulesLeft;
    double stage   = 0.4 + 0.6 * (rulesLeft / 12.0);  // 초기 1.0, 말미 0.4
    double behind  = (lead<0) ? min(0.6, (-lead)/120000.0) : -min(0.4, lead/180000.0);
    double aggro   = 1.0 + behind;                    // 뒤지면 ↑, 앞서면 ↓

    // 금액 매핑: 작은 격차면 0, 큰 격차일수록 선형↑, 상한 100k
    const int   THRESH = 3000;   // 최소 유효차(점수)
    const double SCALE = 1.6;    // 점수→금액 스케일
    double edge = (margin > THRESH) ? (margin - THRESH) : 0.0;
    long long amount = (long long)(edge * SCALE * aggro * stage);

    if (amount < 0) amount = 0;
    if (amount > 100000) amount = 100000;
    return Bid{group, (int)amount};
}


    // ======== 배치 전략 (항상 5개 출력, cnt 기반으로 빠르게) ========
    // ======== 배치 전략 (항상 5개 출력, cnt 기반으로 빠르게) ========
// ======== 배치 전략 (항상 5개 출력, cnt 기반으로 빠르게) ========
DicePut calculatePut() {
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
    auto fillToFiveSmart = [&](vector<int> base, DiceRule target){
        array<int,7> tmp = C;
        for (int d: base) {
            if (!(1<=d && d<=6) || tmp[d]==0) return vector<int>{};
            tmp[d]--;
        }
        auto pushByOrder = [&](const vector<int>& order){
            for (int f: order){
                while(1<=f && f<=6 && tmp[f]>0 && base.size()<5){
                    base.push_back(f); tmp[f]--;
                }
                if (base.size()==5) break;
            }
        };
        auto upperUsed = [&](int f){ return myState.ruleScore[f-1] != -1; };

        // 상단 이미 사용한 얼굴/아직 남은 얼굴 분리
        vector<int> usedFacesDesc, usedFacesAsc, unusedDesc, unusedAsc;
        for (int f=6; f>=1; --f){
            if (upperUsed(f)) usedFacesDesc.push_back(f);
            else              unusedDesc.push_back(f);
        }
        usedFacesAsc  = usedFacesDesc;  reverse(usedFacesAsc.begin(),  usedFacesAsc.end());
        unusedAsc     = unusedDesc;     reverse(unusedAsc.begin(),     unusedAsc.end());

        if (!comboAngle){
            // 각 없음 → 상단 쓴 얼굴을 먼저 태운다
            if (target==CHOICE || target==FOUR_OF_A_KIND){
                // 점수↑가 유리 → 큰 눈 우선
                pushByOrder(usedFacesDesc);
                pushByOrder(unusedDesc);
            } else if (target==SMALL_STRAIGHT){
                // 점수에 영향 적음 → 큰 눈 아껴서 작은 눈부터
                pushByOrder(usedFacesAsc);
                pushByOrder(unusedAsc);
            } else { // 상단 카테고리(ONE~SIX) 보충
                // 해당 카테고리 눈 외의 보충은 작은 눈부터
                pushByOrder(usedFacesAsc);
                pushByOrder(unusedAsc);
            }
        } else {
            // 각 있음 → 기존 기본 정책 유지
            bool prefer_low = (target>=ONE && target<=SIX) || (target==SMALL_STRAIGHT);
            if (prefer_low){
                for (int v=1; v<=6 && base.size()<5; ++v)
                    while(tmp[v]>0 && base.size()<5){ base.push_back(v); tmp[v]--; }
            } else {
                for (int v=6; v>=1 && base.size()<5; --v)
                    while(tmp[v]>0 && base.size()<5){ base.push_back(v); tmp[v]--; }
            }
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
    for (auto &c : cs) {
        if (c.r != FULL_HOUSE) continue;

        std::array<int,7> used{}; 
        for (int d : c.use) if (1<=d && d<=6) used[d]++;

        // 트리플이 어떤 face인지 찾기
        int triFace = -1;
        for (int f=1; f<=6; ++f) if (used[f] >= 3) { triFace = f; break; }
        if (triFace == -1) continue; // 안전장치

        // 그 face의 상단 칸이 비어 있으면 풀하우스 거의 금지
        if (myState.ruleScore[triFace-1] == -1) {
            c.eff -= KILL_FH_IF_TRIPLE_OPEN;

            // 스트레이트 살아있고, 풀하우스가 2~5를 소모하면 추가로 더 감점
            if (straightAlive) {
                int critUse = used[2] + used[3] + used[4] + used[5];
                if (critUse > 0) c.eff -= EXTRA_KILL_IF_USE_2_5;
            }
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
    bool lateGame = (rulesLeft <= 3);

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
    // === (C4) SMALL_STRAIGHT(4연속) 강제 우선 규칙 ===
    // 라지 불가 + 스몰 가능(4연속 포함)이면 스몰에 강한 보정
    auto isSmallPattern = [&](const vector<int>& d)->bool{
        // d(채워진 5개) 안에 1-2-3-4 / 2-3-4-5 / 3-4-5-6 중 하나가 포함되는지 체크
        array<int,7> u{}; for (int x : d) if (1<=x && x<=6) u[x]=1;
        bool a = u[1]&&u[2]&&u[3]&&u[4];
        bool b = u[2]&&u[3]&&u[4]&&u[5];
        bool c = u[3]&&u[4]&&u[5]&&u[6];
        return a||b||c;
    };

    int idxSmall = -1;
    bool hasLarge = false;
    for (int i=0;i<(int)cs.size();++i){
        if (cs[i].r == SMALL_STRAIGHT && isSmallPattern(cs[i].use)) idxSmall = i;
        if (cs[i].r == LARGE_STRAIGHT) hasLarge = true;
    }

    if (idxSmall != -1 && !hasLarge) {
        // 스몰이 상단이나 다른 콤보에 밀리지 않도록 강하게 밀어줌
        const int SMALL4_FORCE = 14000;     // 튜닝 포인트: 10k~18k 사이 권장
        cs[idxSmall].eff += SMALL4_FORCE;

        // (선택) 동시에 애매한 상단(1~2개만 쓰는 상단)은 살짝 눌러준다
        for (auto &c : cs){
            if (c.r>=ONE && c.r<=SIX){
                int face = c.r+1, used=0; for(int d: c.use) if(d==face) ++used;
                if (used <= 2) c.eff -= 4000; // 약한 상단 감점
            }
        }
    }
    // === (C5) 저득점 FULL_HOUSE 강한 회피 규칙 ===
    // 18k 미만(합<18) 풀하우스는 가급적 피하고, 상단/스몰/CHOICE로 유도
    const int FH_FLOOR                = 18000;  // 이 값 미만이면 약함
    const int FH_LOW_BASE             = 6000;   // 기본 패널티
    const int FH_TRI_OPEN_PENALTY     = 9000;   // 트리플의 상단 칸이 비었으면 추가 패널티
    const int FH_PAIR_OPEN_PENALTY    = 4000;   // 페어 얼굴 상단 칸 비었으면 추가
    const int FH_STRAIGHT_CRIT_PENAL  = 4000;   // {2,3,4,5} 소모시(스몰/라지 살아있으면) 추가
    const int FH_CHOICE_NEAR_MARGIN   = 2000;   // CHOICE와 점수 차가 이 이하면
    const int FH_CHOICE_NEAR_PENALTY  = 5000;   // CHOICE 쪽으로 기울이기
    const int FH_LATE_SOFTEN          = 3000;   // 막턴(남은 칸<=2)엔 살짝 완화

    // CHOICE 최고 raw를 미리 구해 CHOICE와 근접하면 FH를 더 깎기
    int bestChoiceRaw = -1;
    for (auto &c : cs) if (c.r == CHOICE) bestChoiceRaw = max(bestChoiceRaw, c.raw);



    for (auto &c : cs) {
        if (c.r != FULL_HOUSE) continue;

        int fhRaw = c.raw; // FULL_HOUSE는 합*1000
        array<int,7> used{}; for (int d: c.use) if (1<=d && d<=6) used[d]++;

        // 트리플/페이스 찾기(5-of-a-kind를 FULL로 인정하는 규칙 고려)
        int triFace=-1, pairFace=-1;
        for (int f=1; f<=6; ++f){
            if (used[f]==5){ triFace=f; pairFace=f; }
            else if (used[f]==3){ triFace=f; }
            else if (used[f]==2){ pairFace=f; }
        }

        int penalty = 0;

        // 1) 점수 하한 미만이면 기본 + (부족분)만큼 추가 패널티
        if (fhRaw < FH_FLOOR) penalty += FH_LOW_BASE + (FH_FLOOR - fhRaw);

        // 2) 상단 칸이 열려 있으면 더 큰 패널티(트리플>페어)
        if (triFace!=-1 && myState.ruleScore[triFace-1]==-1) penalty += FH_TRI_OPEN_PENALTY;
        if (pairFace!=-1 && myState.ruleScore[pairFace-1]==-1) penalty += FH_PAIR_OPEN_PENALTY;

        // 3) 스몰/라지 살아있고 {2,3,4,5}를 소모하면 추가 패널티
        if (straightAlive){
            int critUse = used[2]+used[3]+used[4]+used[5];
            if (critUse > 0) penalty += FH_STRAIGHT_CRIT_PENAL;
        }

        // 4) CHOICE랑 비슷하면 FH 더 깎아서 CHOICE 선택 유도
        if (bestChoiceRaw!=-1 && bestChoiceRaw >= fhRaw - FH_CHOICE_NEAR_MARGIN)
            penalty += FH_CHOICE_NEAR_PENALTY;

        // 5) 막턴이면 약간 완화(채워야 할 수 있으니)
        if (lateGame) penalty -= FH_LATE_SOFTEN;

        c.eff -= penalty;
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

    auto rankRule = [&](DiceRule r)->int{
        static map<DiceRule,int> rank = {
            {YACHT,100},{LARGE_STRAIGHT,95},{FOUR_OF_A_KIND,90},
            {FULL_HOUSE,85},{CHOICE,80},{SMALL_STRAIGHT,75},
            {SIX,20},{FIVE,19},{FOUR,18},{THREE,17},{TWO,16},{ONE,15}
        };
        return rank[r];
    };

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
