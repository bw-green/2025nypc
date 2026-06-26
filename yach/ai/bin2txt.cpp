// bin2txt.cpp (no-arg fallback)
#include <bits/stdc++.h>
#include <filesystem>
using namespace std;

struct LinearPolicy { vector<double> w; };
struct Learner { LinearPolicy setPol, putPol; };

static bool loadParamsBinary(Learner& L, const std::string& path, std::string& why){
    ifstream fin(path, ios::binary);
    if(!fin){ why = "cannot open input file"; return false; }
    char magic[8]; fin.read(magic, 8);
    if(!fin){ why = "failed to read magic"; return false; }
    if(string(magic, magic+8)!="RLPv0001"){ why = "magic mismatch (not RLPv0001)"; return false; }

    int setDim=0; fin.read(reinterpret_cast<char*>(&setDim), sizeof(int));
    if(!fin){ why = "failed to read setDim"; return false; }
    if(setDim<=0 || setDim>100000){ why = "setDim out of range"; return false; }
    L.setPol.w.assign(setDim, 0.0);
    fin.read(reinterpret_cast<char*>(L.setPol.w.data()), sizeof(double)*setDim);
    if(!fin){ why = "failed to read set weights"; return false; }

    int putDim=0; fin.read(reinterpret_cast<char*>(&putDim), sizeof(int));
    if(!fin){ why = "failed to read putDim"; return false; }
    if(putDim<=0 || putDim>100000){ why = "putDim out of range"; return false; }
    L.putPol.w.assign(putDim, 0.0);
    fin.read(reinterpret_cast<char*>(L.putPol.w.data()), sizeof(double)*putDim);
    if(!fin){ why = "failed to read put weights"; return false; }
    return true;
}

static bool saveParamsText(const Learner& L, const std::string& path_txt, std::string& why){
    ofstream fout(path_txt);
    if(!fout){ why = "cannot open output file"; return false; }
    fout.setf(ios::fixed); fout<<setprecision(8);

    fout << "RLTXT v1\n";
    fout << "SET_DIM " << L.setPol.w.size() << "\n";
    fout << "PUT_DIM " << L.putPol.w.size() << "\n";

    fout << "W_SET";
    for(double w : L.setPol.w) fout << " " << w;
    fout << "\n";

    fout << "W_PUT";
    for(double w : L.putPol.w) fout << " " << w;
    fout << "\nEND\n";

    if(!fout){ why = "write failed"; return false; }
    return true;
}

int main(int argc, char** argv){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    // exe와 같은 폴더 기준 기본 경로 설정
    std::filesystem::path exePath = std::filesystem::absolute(argv[0]);
    std::filesystem::path exeDir  = exePath.parent_path();
    std::string in, out;

    if(argc >= 3){
        in  = argv[1];
        out = argv[2];
    } else {
        in  = (exeDir / "data2.bin").string();
        out = (exeDir / "data2.txt").string();
        cerr << "No args provided. Using defaults:\n"
             << "  IN : " << in  << "\n"
             << "  OUT: " << out << "\n";
    }

    Learner L;
    std::string why;
    if(!loadParamsBinary(L, in, why)){
        cerr << "load fail: " << why << "\n";
#ifdef _WIN32
        system("pause");
#endif
        return 2;
    }
    if(!saveParamsText(L, out, why)){
        cerr << "save fail: " << why << "\n";
#ifdef _WIN32
        system("pause");
#endif
        return 3;
    }
    cerr << "ok: wrote " << out
         << " (SET_DIM=" << L.setPol.w.size()
         << ", PUT_DIM=" << L.putPol.w.size() << ")\n";
#ifdef _WIN32
    if(argc < 3) system("pause"); // 더블클릭으로 열었을 때 창이 바로 닫히지 않도록
#endif
    return 0;
}
