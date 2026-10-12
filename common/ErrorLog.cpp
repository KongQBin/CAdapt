#include "ErrorLog.h"
#include <linux/limits.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>


using namespace std;
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
        if (!logFile.is_open()) cerr << "无法打开错误日志，将输出到stderr: " << logFilePath << endl;
    }
    else cerr << "无法确定可执行文件路径，错误日志将输出到stderr。" << endl;
}

// 析构函数已在 .h 中 default

void ErrorLog::putErrInfo(const string& err, const string& err2)
{
    const string line = err + "  文件:" + err2 + "\n";
    if(logFile.is_open())
    {
        logFile << line;
        logFile.flush(); // 确保日志立即写入
        if (logFile.good()) return;
        logFile.close();
        cerr << "错误日志写入失败，改为输出到stderr。" << endl;
    }
    cerr << line;
}

bool ErrorLog::GetExePath(string &strPath, string &strProcessName)
{
    char processdir[PATH_MAX];
    char * path_end;
    ssize_t str_len = 0;

#ifdef __APPLE__
    str_len = proc_pidpath(getpid(), processdir, sizeof(processdir));
#else
    str_len = readlink("/proc/self/exe", processdir, sizeof(processdir) - 1);
#endif

    if(str_len <= 0 || static_cast<size_t>(str_len) >= sizeof(processdir) - 1)
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
