#include "ErrorLog.h"
#include <unistd.h>
#include <cstring>
#include <cstdio>
#ifndef __APPLE__
    #include <linux/limits.h>
#else
    #ifndef PATH_MAX
        #define PATH_MAX 4096
    #endif
#endif

ErrorLog *ErrorLog::getErrorLog()
{
    static ErrorLog errLog;
    return &errLog;
}

ErrorLog::ErrorLog()
{
    string path,name;
    if(GetExePath(path,name))
    {
        fp = fopen(string(path + "/" + name + "Errlog.txt").c_str(),"w");
    }
}

bool ErrorLog::GetExePath(string &strPath, string &strProcessName)
{
    char processdir[PATH_MAX];
    char * path_end;
#ifdef __APPLE__
    char pathBuffer[PROC_PIDPATHINFO_MAXSIZE];
    bzero(pathBuffer, PROC_PIDPATHINFO_MAXSIZE);
    int str_len = proc_pidpath(getpid(), pathBuffer, sizeof(pathBuffer));
    memcpy(processdir, pathBuffer, str_len);
#else
    int str_len = readlink("/proc/self/exe", processdir,PATH_MAX);
#endif
    if(str_len <= 0)
        return false;
    processdir[str_len] = '\0';

    path_end = strrchr(processdir, '/');
    if(path_end == NULL)
        return false;
    ++path_end;

    char processname[PATH_MAX];
    strcpy(processname, path_end);

    strProcessName = processname;
    *path_end = '\0';
    strPath = processdir;
    return true;
}

ErrorLog::~ErrorLog()
{
    if(fp)
    {
        fclose(fp);
        fp = NULL;
    }
}

void ErrorLog::putErrInfo(string err, string err2)
{
    if(fp)
    {
        string line = err + "  文件:" + err2 + "\n";
        fputs(line.c_str(),fp);
    }
    return;
}

