#include <bits/stdc++.h>
using namespace std;
using ll = long long;

int main(){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    ll n;
    cin >> n;
    vector<int> v(n);
    for (int i = 0; i < n; i++) {
        cin >> v[i];
    }
    sort(v.begin(), v.end());
    ll k= (n*(n-1)/2+1)/2;
    int l= v[0]+v[1],r= v[n-1]+v[n-2];
    int dap=0;
    while(l<=r){
        int mid = l + (r - l) / 2;
        ll cnt = 0;
        int ridx = n - 1;
        for(int i = 0; i < n; i++){
            while(ridx > i && v[i] + v[ridx] > mid) ridx--;
            if(ridx <= i) break;
            cnt += (ridx - i);
        }
        if (cnt >= k) {
            dap = mid;
            r = mid - 1;
        } else {
            l = mid + 1;
        }
    }
    cout<<dap;
    return 0;
}
