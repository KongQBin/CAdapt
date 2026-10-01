#pragma once
#include <iostream>
#include <string>
#include <fstream>

class ErrorLog
{
public:
    static ErrorLog *getErrorLog();
    ~ErrorLog() = default;
    void putErrInfo(const std::string& err, const std::string& err2 = "");
private:
    ErrorLog();
    bool GetExePath(std::string& strPath, std::string &strProcessName);

    std::ofstream logFile;
};
typedef ErrorLog* ErrorLogPtr;
