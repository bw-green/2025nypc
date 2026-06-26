#!/usr/bin/env python3
"""
nnue_weights.txt -> data.bin(바이너리 가중치) + nnue_final.cpp(data.bin 로드형 단독 소스).

data.bin 형식 (little-endian):
  int32 IN, int32 H1, int32 H2,  그 뒤 float32 들: W1[H1*IN], b1[H1], W2[H2*H1], b2[H2], W3[H2], b3
nnue_final.cpp 는 같은 폴더의 data.bin 을 자동으로 읽음(인자로 다른 경로 지정 가능).

사용:
  python bake_bin.py            # nnue_weights.txt -> data.bin, nnue_final.cpp
재학습 후 다시 실행하면 data.bin 만 갱신해서 갈아끼우면 됨(재컴파일 불필요).
"""
import struct
import sys

weights = sys.argv[1] if len(sys.argv) > 1 else "nnue_weights.txt"
out_cpp = sys.argv[2] if len(sys.argv) > 2 else "nnue_final.cpp"
out_bin = sys.argv[3] if len(sys.argv) > 3 else "data.bin"

toks = open(weights).read().split()
assert toks[0] == "NNUE", "weights 형식 오류"
IN, H1, H2, OUT = (int(x) for x in toks[2:6])
floats = [float(x) for x in toks[6:]]
need = H1 * IN + H1 + H2 * H1 + H2 + H2 + 1
assert len(floats) == need, f"가중치 개수 불일치 {len(floats)} != {need}"

# --- data.bin ---
with open(out_bin, "wb") as f:
    f.write(struct.pack("<iii", IN, H1, H2))
    f.write(struct.pack(f"<{len(floats)}f", *floats))
print(f"{out_bin}: {len(floats):,} floats, {12 + 4*len(floats):,} bytes")

# --- nnue_final.cpp (data.bin 로드형) ---
src = open("nnue_bot.cpp", encoding="utf-8").read()

anchor = "        loaded=true; return true;\n    }\n"
assert anchor in src, "load() 앵커 못 찾음"
loadbin = anchor + (
    "\n    bool loadBin(const char* path){\n"
    "        FILE* f=fopen(path,\"rb\"); if(!f)return false;\n"
    "        int in,h1,h2;\n"
    "        if(fread(&in,4,1,f)!=1||fread(&h1,4,1,f)!=1||fread(&h2,4,1,f)!=1){fclose(f);return false;}\n"
    "        if(in!=IN||h1!=H1||h2!=H2){fclose(f);return false;}\n"
    "        auto rd=[&](vector<float>&v){return fread(v.data(),sizeof(float),v.size(),f)==v.size();};\n"
    "        bool ok=rd(W1)&&rd(b1)&&rd(W2)&&rd(b2)&&rd(W3) && (fread(&b3,4,1,f)==1);\n"
    "        fclose(f); loaded=ok; return ok;\n"
    "    }\n"
)
src = src.replace(anchor, loadbin, 1)

oldload = "    if(argc>1) NET.load(argv[1]);\n"
assert oldload in src, "main 로드 라인 못 찾음"
src = src.replace(
    oldload,
    '    const char* wpath=(argc>1)?argv[1]:"data.bin";\n'
    '    if(!NET.loadBin(wpath)) NET.load(wpath);   // data.bin(바이너리) 우선, 실패시 텍스트\n',
    1,
)

header = ("// 자동생성: nnue_bot.cpp + data.bin 로드형 단독 봇.\n"
          "// 같은 폴더의 data.bin 을 읽음(인자로 경로 지정 가능). 재학습시 data.bin 만 교체.\n"
          "// 컴파일: g++ -std=c++17 -O2 -o nnue_final nnue_final.cpp\n")
open(out_cpp, "w", encoding="utf-8").write(header + src)
print("생성:", out_cpp)
