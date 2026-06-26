# 봇 리그(풀리그) 작업 노트 — 2026-06-26

NYPC 2026 게임(10×17 보드, 합10 직사각형 점령) 봇들을 서로 풀리그로
대국시켜 강함을 비교하는 작업. 이 문서만 보면 다시 이어서 할 수 있게 정리함.

---

## 1. 핵심 파일 (모두 `ai/` 안)

| 파일 | 역할 |
|---|---|
| `ai/referee.py` | 심판/시뮬레이터. 게임 규칙·시간측정·프로토콜의 핵심 엔진 |
| `ai/league.py`  | 리그 진행기. `referee`를 import해서 사용 → **둘은 같은 폴더에 있어야 함** |
| `ai/<id>.cpp`   | 봇 소스 (예: `18913.cpp`, `19405.cpp`, `19931.cpp`, `19941.cpp`, `20134.cpp`, `20138.cpp`) |
| `ai/<id>.exe`   | 봇 실행파일. `league.py`가 `ai/<id>.exe`를 직접 실행 |

봇들은 외부 가중치 파일을 안 읽는 자체 완결형이라 그 외 파일은 불필요.

## 2. 도구
- **g++** (msys2 mingw64, `g++ 14.2.0`) — C++17 컴파일
- **파이썬 3.x** — Windows에서 `python`은 Store 스텁이라 안 됨. 반드시 **`py`** 로 실행 (3.13.2)

## 3. 컴파일
`2026/` 에서:
```bash
for f in 18913 19405 19931 19941 20134 20138; do
  g++ -std=c++17 -O2 -o ai/$f.exe ai/$f.cpp
done
```
(새 봇 추가 시 그 `.cpp`만 같은 식으로 컴파일)

## 4. 실행
```bash
py ai/league.py --boards 30 --time 10000 --workers 4 --seed 20270000
```
옵션:
- `--boards N` : 보드 수. **총게임 = N × 6쌍 × 2석** (4봇 기준). 예: 30보드 → 360게임
- `--time 10000` : 봇당 총 사고시간(ms), 초과 시 몰수패(strict)
- `--workers 4` : 동시 게임 수. **8코어 기준 4 권장** (봇 2개×4=8프로세스). 더 키우면 CPU 경합으로 몰수 왜곡
- `--seed` : 보드 시드 베이스. 값이 다르면 다른 맵 (보드 b = `random_board(seed+b)`)

**참가 봇 변경 = `ai/league.py` 맨 위 `BOTS` 리스트 수정**
```python
BOTS = ["20134", "19405", "19931", "20138"]
```

## 5. 로그 (출력 형식)
`ai/logs/<날짜>/` 에 저장.
- 게임마다 **개별 폴더에 즉시 저장** (끝에 한꺼번에 모아 저장하지 않음).
  - 폴더명: `g<순번>_b<보드>_<선공>-vs-<후공>/game.txt`
- `game.txt` 포맷 (심판 프로토콜 스타일):
  ```
  WINNER FIRST 20134            ← 맨 앞: 승자 (FIRST/SECOND + 봇id, 또는 DRAW)
  INIT <17자리 x 10줄>          ← 보드
  FIRST  3 0 7 0 1267           ← 점령 사각형(r1 c1 r2 c2) + 그 수에 걸린 ms
  SECOND 2 8 5 8 1401
  ...
  FIRST  -1 -1 -1 -1 246        ← PASS
  FINISH
  SCOREFIRST 53                 ← 선공 점령 칸 수
  SCORESECOND 36                ← 후공 점령 칸 수
  ```
  (몰수 게임은 WINNER 줄에 사유가 붙고, referee가 부분 수순을 안 돌려줘서 move 줄이 없음)
- `summary.txt` : 순위표·헤드투헤드·좌석/몰수 요약 (리그 끝까지 완주해야 생성됨)

### 승률 정의
순위표의 **`승률` = 승 / 전체게임** (무승부도 분모에 포함). 승점 = 승2·무1·패0.

## 6. 중간 평가 / 정산 (summary.txt 없이 완료분만으로 집계)
완료된 게임 폴더만 읽어 순위를 계산하는 스니펫. `BOTS`만 현재 라인업으로 맞추면 됨.
```python
import glob, os, re
BOTS = ["20134","19405","19931","20138"]   # 현재 라인업으로 수정
base = r"C:\Users\bwgre\2025nypc\2026\ai\logs\2026-06-26"  # 날짜 수정
W={n:0 for n in BOTS}; L={n:0 for n in BOTS}; D={n:0 for n in BOTS}; P={n:0 for n in BOTS}
H={a:{b:0 for b in BOTS} for a in BOTS}; ng=0
for d in glob.glob(os.path.join(base,"g*")):
    gt=os.path.join(d,"game.txt")
    if not os.path.isfile(gt): continue
    m=re.match(r"g\d+_b\d+_(\d+)-vs-(\d+)$", os.path.basename(d))
    if not m: continue
    f,s=m.group(1),m.group(2); w0=open(gt,encoding="utf-8").readline(); ng+=1
    if w0.startswith("WINNER DRAW"): D[f]+=1;D[s]+=1;P[f]+=1;P[s]+=1
    elif w0.startswith("WINNER FIRST"): W[f]+=1;L[s]+=1;P[f]+=2;H[f][s]+=1
    elif w0.startswith("WINNER SECOND"): W[s]+=1;L[f]+=1;P[s]+=2;H[s][f]+=1
print("완료", ng, "게임")
for n in sorted(BOTS,key=lambda n:(P[n],W[n]),reverse=True):
    pl=W[n]+D[n]+L[n]
    print(f"{n}  승점{P[n]}  {W[n]}승 {D[n]}무 {L[n]}패  승률 {W[n]/pl*100:.1f}%")
```
실행: `py 위파일.py`

## 7. 진행 중 멈추기 (재시작/봇교체 시)
```powershell
Get-CimInstance Win32_Process -Filter "name='python.exe' OR name='py.exe'" |
  Where-Object { $_.CommandLine -like '*league.py*' } |
  ForEach-Object { taskkill /PID $_.ProcessId /T /F }
Get-Process -Name 20134,19405,19931,20138 -ErrorAction SilentlyContinue | Stop-Process -Force
```

---

## 8. 오늘 진행한 것 (타임라인)
- referee.py / league.py 정비. `python`→`py` 사용, g++로 6개 봇 컴파일.
- **로그 포맷을 진화시킴**: 요약 한방 저장 → 게임별 개별 폴더 즉시 저장 →
  심판 프로토콜 스타일(WINNER/INIT/FIRST·SECOND+ms/FINISH/SCORE...)로 변경.
  - 이를 위해 `referee.play_game`의 `moves`에 **수별 걸린시간(ms)** 추가 기록.
- **승률 정의 변경**: 결정승률(승/(승+패)) → 그냥 승률(승/전체, 무승부 포함).
- 라인업 바꿔가며 여러 번 리그를 돌림:
  1. `18913·19405·19931·19941` 20보드(240게임) 완주 → **19405 압도적 우승**(68%).
     단 20보드는 표본이 작아 운이 컸던 것으로 판단.
  2. 같은 4봇 100보드(1200게임) 시도 → 중간(~32%)에서 **박빙**(18913 근소 선두,
     19405 꼴찌권)으로 20보드와 상반됨 → 도중 중단.
  3. `20134·19405·19931·18913` 30보드 → 207게임 완료분 정산:
     **20134 1위**(승점116, 54.3%), 19405 2위, 18913 3위, 19931 4위(몰수 5건).
- 큰 시사점:
  - **새 봇 `20134`이 강함** (라인업3에서 1위, 맞대결 우위).
  - **선공 이점이 매우 큼** (전 봇 선공승 ≫ 후공승). seat-paired라 공정성엔 영향 없음.
  - 2초 같은 짧은 시간제한에선 선공이 미세 시간초과로 전부 몰수됨 → **10000ms 사용**.

## 9. 현재 상태 (이 문서 작성 시점)
- **실행 중**: `20134·19405·19931·20138` (18913→20138 교체), **30보드(360게임)**,
  시드 20270000, 10000ms, 워커4.
- 로그: `ai/logs/2026-06-26/` 에 게임별 폴더로 쌓이는 중 (작성 시점 ~28게임 완료).
- **이어서 하려면**: 리그가 끝나면 `summary.txt` 확인, 또는 6번 스니펫으로 완료분 정산.
  중간에 멈추려면 7번 명령 사용.

## 10. 주의/한계
- 시간 모드 병렬은 CPU 경합으로 결과가 흔들릴 수 있음 → 워커 4 유지, 완전 공정 비교는 `--workers 1`.
- 몰수 게임은 수순(move 줄)이 기록되지 않음 (referee가 부분 수순 미반환).
- 같은 시드·같은 보드라도 봇이 10초 사고에서 비결정적이라 결과가 매번 약간 달라질 수 있음.
- 표본이 작으면(20~30보드) 운에 좌우됨. 신뢰도 높이려면 보드 수를 키울 것(시간은 비례 증가).
