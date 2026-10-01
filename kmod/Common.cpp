#include "Common.h"
using namespace std;
list<string> splitString(const string &str, char delimiter)
{
    string token;
    list<string> tokens;
    stringstream ss(str);
    while (getline(ss, token, delimiter)) {
        if(token.size()) tokens.push_back(token);
    }
    return tokens;
}
