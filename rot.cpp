#include <bits/stdc++.h>
using namespace std;

string s = "lbqqsyljystki{feaubgavnlwawdnpxypn}";

char atbash(char c) {
    if (c >= 'a' && c <= 'z')
        return char('z' - (c - 'a'));
    return c;
}

int main() {
    for (char &c : s)
        c = atbash(c);

    cout << s << "\n";
    return 0;
}
