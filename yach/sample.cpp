#include <algorithm>
#include <cassert>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

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
    vector<int> dice;  // 배치할 주사위 목록
};

// 팀의 현재 상태를 관리하는 구조체
struct GameState {
    vector<int> dice;   // 현재 보유한 주사위 목록
    vector<int> ruleScore;  // 각 규칙별 획득 점수 (사용하지 않았다면 -1)
    int bidScore;       // 입찰로 얻거나 잃은 총 점수

    // 처음에 사용하지 않은 상태로 score를 초기화
    GameState() : ruleScore(12, -1), bidScore(0) {}

    int getTotalScore() const;                // 현재까지 획득한 총 점수 계산 (상단/하단 점수 + 보너스 + 입찰 점수)
    void bid(bool isSuccessful, int amount);  // 입찰 결과에 따른 점수 반영
    void addDice(vector<int> newDice);        // 새로운 주사위들을 보유 목록에 추가
    void useDice(DicePut put);                // 주사위를 사용하여 특정 규칙에 배치
    static int calculateScore(DicePut put);   // 주어진 규칙과 주사위에 대한 점수 계산
};

// 게임 상태를 관리하는 클래스
class Game {
   public:
    GameState myState;   // 내 팀의 현재 상태
    GameState oppState;  // 상대 팀의 현재 상태

    // ================================ [필수 구현] ================================
    // ============================================================================
    // 주사위가 주어졌을 때, 어디에 얼마만큼 베팅할지 정하는 함수
    // 입찰할 그룹과 베팅 금액을 pair로 묶어서 반환
    // ============================================================================
    Bid calculateBid(vector<int> diceA, vector<int> diceB) {
        // 합이 높은 쪽에 배팅
        int sumA = 0;
        for (int d : diceA) sumA += d;
        int sumB = 0;
        for (int d : diceB) sumB += d;
        char group = (sumA > sumB) ? 'A' : 'B';

        // (내 현재 점수 - 상대 현재 점수) / 10을 0이상 100000이하로 잘라서 배팅
        int amount = (myState.getTotalScore() - oppState.getTotalScore()) / 10;
        if (amount < 0) amount = 0;
        if (amount > 100000) amount = 100000;

        return Bid{group, amount};
    }

    // ============================================================================
    // 주어진 주사위에 대해 사용할 규칙과 주사위를 정하는 함수
    // 사용할 규칙과 사용할 주사위의 목록을 pair로 묶어서 반환
    // ============================================================================
    DicePut calculatePut() {
        // 사용하지 않은 첫 규칙 찾기
        int rule = -1;
        for (int i = 0; i < 12; i++)
            if (myState.ruleScore[i] == -1) {
                rule = i;
                break;
            }
        // 처음 5개 주사위 사용
        vector<int> dice(myState.dice.begin(), myState.dice.begin() + 5);
        return DicePut{DiceRule(rule), dice};
    }
    // ============================== [필수 구현 끝] ==============================

    // 입찰 결과를 받아서 상태 업데이트
    void updateGet(vector<int> diceA, vector<int> diceB, Bid myBid, Bid oppBid, char myGroup) {
        // 그룹에 따라 주사위 분배
        if (myGroup == 'A')
            myState.addDice(diceA), oppState.addDice(diceB);
        else
            myState.addDice(diceB), oppState.addDice(diceA);

        // 입찰 결과에 따른 점수 반영
        bool myBidOk = myBid.group == myGroup;
        myState.bid(myBidOk, myBid.amount);

        char oppGroup = myGroup == 'A' ? 'B' : 'A';
        bool oppBidOk = oppBid.group == oppGroup;
        oppState.bid(oppBidOk, oppBid.amount);
    }

    // 내가 주사위를 배치한 결과 반영
    void updatePut(DicePut put) { myState.useDice(put); }

    // 상대가 주사위를 배치한 결과 반영
    void updateSet(DicePut put) { oppState.useDice(put); }
};

// 현재까지 획득한 총 점수 계산 (상단/하단 점수 + 보너스 + 입찰 점수)
int GameState::getTotalScore() const {
    int basic = 0, combination = 0, bonus = 0;

    // 기본 점수 규칙 계산 (ONE ~ SIX)
    for (int i = 0; i < 6; i++)
        if (ruleScore[i] != -1) basic += ruleScore[i];
    // 보너스 점수 계산 (기본 규칙 63000점 이상시 35000점 보너스)
    if (basic >= 63000) bonus += 35000;
    // 조합 점수 규칙 계산 (CHOICE ~ YACHT)
    for (int i = 6; i < 12; i++)
        if (ruleScore[i] != -1) combination += ruleScore[i];

    return basic + bonus + combination + bidScore;
}

// 입찰 결과에 따른 점수 반영
void GameState::bid(bool isSuccessful, int amount) {
    if (isSuccessful)
        bidScore -= amount;  // 성공시 베팅 금액만큼 점수 차감
    else
        bidScore += amount;  // 실패시 베팅 금액만큼 점수 획득
}

// 새로운 주사위들을 보유 목록에 추가
void GameState::addDice(vector<int> newDice) {
    for (int d : newDice) dice.push_back(d);
}

// 주사위를 사용하여 특정 규칙에 배치
void GameState::useDice(DicePut put) {
    // 이미 사용한 규칙인지 확인
    assert(ruleScore[put.rule] == -1 && "Rule already used");

    for (int d : put.dice) {
        // 주사위 목록에 없는 주사위가 있는지 확인하고 주사위 제거
        auto it = find(dice.begin(), dice.end(), d);
        assert(it != dice.end() && "Invalid dice");
        dice.erase(it);
    }

    // 해당 규칙의 점수 계산 및 저장
    ruleScore[put.rule] = calculateScore(put);
}

// 주어진 규칙과 주사위에 대한 점수 계산
int GameState::calculateScore(DicePut put) {
    DiceRule rule = put.rule;
    vector<int> dice = put.dice;

    switch (rule) {
        // 기본 규칙 점수 계산 (해당 숫자의 개수 × 숫자 × 1000점)
        case ONE:   return count(dice.begin(), dice.end(), 1) * 1 * 1000;
        case TWO:   return count(dice.begin(), dice.end(), 2) * 2 * 1000;
        case THREE: return count(dice.begin(), dice.end(), 3) * 3 * 1000;
        case FOUR:  return count(dice.begin(), dice.end(), 4) * 4 * 1000;
        case FIVE:  return count(dice.begin(), dice.end(), 5) * 5 * 1000;
        case SIX:   return count(dice.begin(), dice.end(), 6) * 6 * 1000;

        case CHOICE:  // 주사위에 적힌 모든 수의 합 × 1000점
            return accumulate(dice.begin(), dice.end(), 0) * 1000;
        case FOUR_OF_A_KIND: {  // 같은 수가 적힌 주사위가 4개 있다면, 주사위에 적힌 모든 수의 합 × 1000점, 아니면 0
            bool ok = false;
            for (int i = 1; i <= 6; i++)
                if (count(dice.begin(), dice.end(), i) >= 4) ok = true;
            return ok ? accumulate(dice.begin(), dice.end(), 0) * 1000 : 0;
        }
        case FULL_HOUSE: {  // 3개의 주사위에 적힌 수가 서로 같고, 다른 2개의 주사위에 적힌 수도 서로 같으면 주사위에 적힌 모든 수의 합 × 1000점, 아닐 경우 0점
            bool pair = false, triple = false;
            for (int i = 1; i <= 6; i++) {
                int cnt = count(dice.begin(), dice.end(), i);
                // 5개 모두 같은 숫자일 때도 인정
                if (cnt == 2 || cnt == 5) pair = true; 
                if (cnt == 3 || cnt == 5) triple = true;       
            }
            return (pair && triple) ? accumulate(dice.begin(), dice.end(), 0) * 1000 : 0;
        }
        case SMALL_STRAIGHT: {  // 4개의 주사위에 적힌 수가 1234, 2345, 3456중 하나로 연속되어 있을 때, 15000점, 아닐 경우 0점
            bool e1 = count(dice.begin(), dice.end(), 1) > 0;
            bool e2 = count(dice.begin(), dice.end(), 2) > 0;
            bool e3 = count(dice.begin(), dice.end(), 3) > 0;
            bool e4 = count(dice.begin(), dice.end(), 4) > 0;
            bool e5 = count(dice.begin(), dice.end(), 5) > 0;
            bool e6 = count(dice.begin(), dice.end(), 6) > 0;
            bool ok = (e1 && e2 && e3 && e4) || (e2 && e3 && e4 && e5) ||
                      (e3 && e4 && e5 && e6);
            return ok ? 15000 : 0;
        }
        case LARGE_STRAIGHT: {  // 5개의 주사위에 적힌 수가 12345, 23456중 하나로 연속되어 있을 때, 30000점, 아닐 경우 0점
            bool e1 = count(dice.begin(), dice.end(), 1) > 0;
            bool e2 = count(dice.begin(), dice.end(), 2) > 0;
            bool e3 = count(dice.begin(), dice.end(), 3) > 0;
            bool e4 = count(dice.begin(), dice.end(), 4) > 0;
            bool e5 = count(dice.begin(), dice.end(), 5) > 0;
            bool e6 = count(dice.begin(), dice.end(), 6) > 0;
            bool ok = (e1 && e2 && e3 && e4 && e5) || (e2 && e3 && e4 && e5 && e6);
            return ok ? 30000 : 0;
        }
        case YACHT: {  // 5개의 주사위에 적힌 수가 모두 같을 때 50000점, 아닐 경우 0점
            bool ok = false;
            for (int i = 1; i <= 6; i++)
                if (count(dice.begin(), dice.end(), i) == 5) ok = true;
            return ok ? 50000 : 0;
        }
    }
    assert(false);
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
    assert(!"Invalid Dice Rule");  // 올바르지 않은 주사위 규칙
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
    assert(!"Invalid Dice Rule");  // 올바르지 않은 주사위 규칙
}

// 표준 입력을 통해 명령어를 처리하는 메인 함수
int main() {
    Game game;

    // 입찰 라운드에서 나온 주사위들
    vector<int> diceA, diceB;
    // 내가 마지막으로 한 입찰 정보
    Bid myBid;

    while (true) {
        string line;
        getline(cin, line);

        istringstream iss(line);
        string command;
        if (!(iss >> command)) continue;

        if (command == "READY") {
            // 게임 시작
            cout << "OK" << endl;
            continue;
        }

        if (command == "ROLL") {
            // 주사위 굴리기 결과 받기
            string strA, strB;
            iss >> strA >> strB;
            diceA.clear();
            diceB.clear();
            for (char c : strA) diceA.push_back(c - '0');  // 문자를 숫자로 변환
            for (char c : strB) diceB.push_back(c - '0');  // 문자를 숫자로 변환
            myBid = game.calculateBid(diceA, diceB);
            cout << "BID " << myBid.group << " " << myBid.amount << endl;
            continue;
        }

        if (command == "GET") {
            // 주사위 받기
            char getGroup;
            char oppGroup;
            int oppScore;
            iss >> getGroup >> oppGroup >> oppScore;
            game.updateGet(diceA, diceB, myBid, Bid{oppGroup, oppScore}, getGroup);
            continue;
        }

        if (command == "SCORE") {
            // 주사위 골라서 배치하기
            DicePut put = game.calculatePut();
            game.updatePut(put);
            cout << "PUT " << toString(put.rule) << " ";
            for (int d : put.dice) cout << d;
            cout << endl;
            continue;
        }

        if (command == "SET") {
            // 상대의 주사위 배치
            string rule, str;
            vector<int> dice;
            iss >> rule >> str;
            for (char c : str) dice.push_back(c - '0');  // 문자를 숫자로 변환
            game.updateSet(DicePut{fromString(rule), dice});
            continue;
        }

        if (command == "FINISH") {
            // 게임 종료
            break;
        }

        // 알 수 없는 명령어 처리
        cerr << "Invalid command: " << command << endl;
        return 1;
    }

    return 0;
}
