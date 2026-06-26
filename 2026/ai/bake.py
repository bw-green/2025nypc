#!/usr/bin/env python3
"""
학습된 가중치(nnue_weights.txt)를 nnue_bot.cpp 에 정적으로 박아
'외부 파일 없이 단독으로 도는 한 개 파일'(nnue_final.cpp)을 생성.

사용:
  python bake.py [weights_file] [out_cpp]
    기본: nnue_weights.txt -> nnue_final.cpp
재학습 후 다시 실행하면 새 가중치로 다시 구울 수 있음.
"""
import sys

weights = sys.argv[1] if len(sys.argv) > 1 else "nnue_weights.txt"
out_cpp = sys.argv[2] if len(sys.argv) > 2 else "nnue_final.cpp"
src_cpp = "nnue_bot.cpp"

# --- 가중치 파싱 ---
toks = open(weights).read().split()
assert toks[0] == "NNUE", "weights 형식 오류"
IN, H1, H2, OUT = (int(x) for x in toks[2:6])
floats = toks[6:]
need = H1 * IN + H1 + H2 * H1 + H2 + H2 + 1
assert len(floats) == need, f"가중치 개수 불일치: {len(floats)} != {need}"

embed = ("static const float EMBED[] = {\n"
         + ",".join(floats) + "\n};\n"
         "// EMBED 순서: W1[H1*IN], b1[H1], W2[H2*H1], b2[H2], W3[H2], b3\n"
         "static void loadEmbeddedWeights(){\n"
         "    const float* e = EMBED; size_t k = 0;\n"
         "    for(auto&x:NET.W1)x=e[k++]; for(auto&x:NET.b1)x=e[k++];\n"
         "    for(auto&x:NET.W2)x=e[k++]; for(auto&x:NET.b2)x=e[k++];\n"
         "    for(auto&x:NET.W3)x=e[k++]; NET.b3=e[k++];\n"
         "    NET.loaded = true;\n"
         "}\n")

# --- 소스 변형 ---
src = open(src_cpp, encoding="utf-8").read()

anchor = "Net NET;\n"
assert anchor in src, "Net NET; 앵커를 찾지 못함"
src = src.replace(anchor, anchor + "\n" + embed + "\n", 1)

old_load = "    if(argc>1) NET.load(argv[1]);\n"
assert old_load in src, "가중치 로드 라인을 찾지 못함"
src = src.replace(old_load, "    loadEmbeddedWeights();   // 정적 내장 가중치\n", 1)

header = ("// === 자동 생성: nnue_bot.cpp + " + weights + " 를 한 파일로 통합 ===\n"
          "// 외부 파일 불필요. 컴파일: g++ -std=c++17 -O2 -o nnue_final nnue_final.cpp\n")
src = header + src

open(out_cpp, "w", encoding="utf-8").write(src)
print(f"생성됨: {out_cpp}  (가중치 {len(floats):,}개, IN={IN} H1={H1} H2={H2})")
