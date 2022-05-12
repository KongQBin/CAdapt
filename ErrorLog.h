#pragma once
#include <iostream>
#include <string>
using namespace std;

class ErrorLog
{
public:
    static ErrorLog *getErrorLog();
    ~ErrorLog();
    void putErrInfo(string err,string err2 = "");
private:
    ErrorLog();
    FILE *fp = NULL;
    bool GetExePath(string& strPath,string &strProcessName);
};
typedef ErrorLog* ErrorLogPtr;
