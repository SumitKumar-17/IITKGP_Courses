#include <bits/stdc++.h>
using namespace std;

int main() {
    string c = "sfbbguzajvaot{qswisrdcrwhosreaafty}";

    unordered_map<char,char> m = {
        {'s','h'}, {'f','e'}, {'b','l'}, {'g','o'}, {'u','_'},
        {'z','f'}, {'a','r'}, {'j','i'}, {'v','e'}, {'o','d'},
        {'t','n'}, {'q','y'}, {'w','u'}, {'i','o'}, {'r','a'},
        {'d','r'}, {'c','e'}, {'h','n'}, {'e','t'}, {'y','e'}
    };

    for(char x : c) {
        if(m.count(x)) cout << m[x];
        else cout << x;
    }
    cout << '\n';
}
