#include "ErrorLog.h"
#include <unistd.h>
#include <cstring>
#include <cstdio> // GetExePath 仍在使用C-Style API

#ifndef __APPLE__
    #include <linux/limits.h>
#else
    #ifndef PATH_MAX
        #define PATH_MAX 4096
    #endif
    #include <libproc.h> // for proc_pidpath
#endif

ErrorLog *ErrorLog::getErrorLog()
{
    static ErrorLog errLog;
    return &errLog;
}

ErrorLog::ErrorLog()
{
    string path, name;
    if(GetExePath(path, name))
    {
        string logFilePath = path + "/" + name + "Errlog.txt";
        // 构造时打开文件
        logFile.open(logFilePath.c_str(), ios::out | ios::trunc);
    }
}

// 析构函数已在 .h 中 default

void ErrorLog::putErrInfo(const string& err, const string& err2)
{
    if(logFile.is_open())
    {
        string line = err + "  文件:" + err2 + "\n";
        logFile << line;
        logFile.flush(); // 确保日志立即写入
    }
}

bool ErrorLog::GetExePath(string &strPath, string &strProcessName)
{
    char processdir[PATH_MAX];
    char * path_end;
    int str_len = 0;

#ifdef __APPLE__
    str_len = proc_pidpath(getpid(), processdir, sizeof(processdir));
#else
    str_len = readlink("/proc/self/exe", processdir, PATH_MAX);
#endif

    if(str_len <= 0)
        return false;
    processdir[str_len] = '\0';

    path_end = strrchr(processdir, '/');
    if(path_end == nullptr)
        return false;
    
    // 提取进程名
    strProcessName = (path_end + 1);
    
    // 提取路径
    *path_end = '\0';
    strPath = processdir;
    
    return true;
}
