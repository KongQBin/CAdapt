#pragma once
#include <iostream>
#include <string>
#include <fstream> // C++98
using namespace std;

class ErrorLog
{
public:
    static ErrorLog *getErrorLog();
    ~ErrorLog() = default; // C++11
    void putErrInfo(const string& err, const string& err2 = "");
private:
    ErrorLog();
    bool GetExePath(string& strPath, string &strProcessName);

    ofstream logFile; // C++98
};
typedef ErrorLog* ErrorLogPtr;
