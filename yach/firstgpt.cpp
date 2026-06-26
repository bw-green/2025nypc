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
    Bid calculateBid(const vector<int>& diceA, const vector<int>& diceB) {
        int sumA = accumulate(diceA.begin(), diceA.end(), 0);
        int sumB = accumulate(diceB.begin(), diceB.end(), 0);
        char group = (sumA > sumB) ? 'A' : (sumB > sumA ? 'B' : 'A'); // 동률이면 A

        int sumDiff = abs(sumA - sumB);                 // 0~30
        int lead    = myState.getTotalScore() - oppState.getTotalScore();

        long long base = (sumDiff <= 2 ? 0 : (sumDiff - 2) * 200LL); // 보수적
        long long adjust = (lead >= 0 ? -(lead / 40) : (-lead) / 20);
        long long amount = base + adjust;

        if (amount < 0) amount = 0;
        if (amount > 100000) amount = 100000;
        return Bid{group, (int)amount};
    }

    // ======== 배치 전략 (항상 5개 출력, cnt 기반으로 빠르게) ========
    DicePut calculatePut() {
        vector<int> usable;
        for (int r = 0; r < 12; ++r) if (myState.ruleScore[r] == -1) usable.push_back(r);
        if (usable.empty()) return DicePut{ONE, {}};

        // 현재 보유 총량(안전)
        int totalDice = 0; for (int v=1; v<=6; ++v) totalDice += myState.cnt[v];
        if (totalDice < 5) { // 심판이 5개 요구하므로 이 상황은 비정상. 그냥 아무 규칙 0점으로(빈 출력 방지용으로 채워넣기 시도)
            // 가능한 만큼 채워서 5개 맞춤
            vector<int> any5;
            auto tmp = myState.cnt;
            for (int v=6; v>=1 && (int)any5.size()<5; --v){
                while(tmp[v]>0 && (int)any5.size()<5){ any5.push_back(v); tmp[v]--; }
            }
            while((int)any5.size()<5) any5.push_back(1); // 이 줄은 이론상 실행되지 않아야 함
            return DicePut{ (DiceRule)usable[0], any5 };
        }

        const auto &C = myState.cnt;

        auto build_multi = [&](int v, int k){
            vector<int> out; out.reserve(k);
            for (int i=0;i<k;i++) out.push_back(v);
            return out;
        };
        auto fillToFive = [&](vector<int> base){
            // 현재 cnt에서 base를 먼저 소모하고, 큰 눈부터 채워서 길이 5로 만든다.
            array<int,7> tmp = C;
            for (int d: base) {
                if (!(1<=d && d<=6) || tmp[d]==0) return vector<int>{}; // 불가능한 선택
                tmp[d]--;
            }
            for (int v=6; v>=1 && (int)base.size()<5; --v){
                while(tmp[v]>0 && (int)base.size()<5){ base.push_back(v); tmp[v]--; }
            }
            if (base.size()!=5) return vector<int>{}; // 이론상 불가
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
        auto selectFourKind = [&](){
            for (int v = 6; v >= 1; --v) if (C[v] >= 4) {
                int extra=-1; for (int x=6; x>=1; --x) if (x!=v && C[x]>0){ extra=x; break; }
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
        auto selectChoice = [&](){ return pickTop5_from_cnt(); };
        auto selectUpper  = [&](int face){
            int use = min(5, C[face]); // 여기선 face로 가능한 만큼만 모으고
            return vector<int>(use, face);
        };

        auto ruleUsable = [&](DiceRule r){ return myState.ruleScore[r]==-1; };

        struct Cand { DiceRule r; vector<int> use; int score; };
        vector<Cand> cs; cs.reserve(20);
        auto pushIf = [&](DiceRule r, vector<int> d){
            if (!ruleUsable(r)) return;
            if (d.empty() && !(ONE<=r && r<=SIX)) return; // 비상단은 빈 배치 금지
            // 항상 5개로 채움
            d = fillToFive(d);
            if (d.empty()) return;
            cs.push_back({r, d, GameState::calculateScore(r, d)});
        };

        // 우선순위대로 후보 생성(이미 5개인 규칙은 그대로, SMALL/상단은 fillToFive로 5개 보장)
        pushIf(YACHT,           selectYacht());
        pushIf(LARGE_STRAIGHT,  selectLarge());
        pushIf(FOUR_OF_A_KIND,  selectFourKind());
        pushIf(FULL_HOUSE,      selectFullHouse());
        pushIf(CHOICE,          selectChoice());
        pushIf(SMALL_STRAIGHT,  selectSmall());
        for (int face=6; face>=1; --face) pushIf((DiceRule)(face-1), selectUpper(face));

        if (cs.empty()) {
            // 이론상 오지 않음. 그래도 방어: 아무 규칙에 top5 배치
            auto any5 = pickTop5_from_cnt();
            if ((int)any5.size()<5) any5 = fillToFive(any5);
            return DicePut{ (DiceRule)usable[0], any5 };
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
            if (a.score != b.score) return a.score > b.score;
            int ra = rankRule(a.r), rb = rankRule(b.r);
            if (ra != rb) return ra > rb;
            int sa = accumulate(a.use.begin(), a.use.end(), 0);
            int sb = accumulate(b.use.begin(), b.use.end(), 0);
            if (sa != sb) return sa > sb;
            return (int)a.use.size() > (int)b.use.size();
        });

        return DicePut{ cs[0].r, cs[0].use }; // 길이 5 보장
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
    if (isSuccessful) bidScore -= amount;  // 성공시 차감
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

// 표준 입력을 통해 명령어를 처리하는 메인 함수 (네가 준 그대로)
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
