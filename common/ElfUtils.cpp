#include "ElfUtils.h"
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include "ErrorLog.h"

bool ElfUtils::collectElfFiles(const std::string& dir, std::vector<std::string>& out)
{
    DIR *d;
    struct dirent *file;
    struct stat sb;
    std::string filePath;

    if(!(d = opendir(dir.c_str())))
    {
        ErrorLog::getErrorLog()->putErrInfo("扫描目录失败", dir);
        return false;
    }
    bool success = true;
    while(true)
    {
        errno = 0;
        file = readdir(d);
        if (!file)
        {
            if (errno != 0)
            {
                ErrorLog::getErrorLog()->putErrInfo("读取目录失败", dir);
                success = false;
            }
            break;
        }
        if(strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0)
            continue;
        filePath = dir + "/" + file->d_name;
        if(lstat(filePath.c_str(), &sb) >= 0)
        {
            if(S_ISLNK(sb.st_mode)) continue;
            if(S_ISDIR(sb.st_mode))
            {
                if (!collectElfFiles(filePath, out)) success = false;
            }
            else if(S_ISREG(sb.st_mode))
            {
                const int result = isElfFile(filePath.c_str());
                if (result < 0) success = false;
                else if (result > 0)
                    out.push_back(filePath);
            }
        }
        else
        {
            ErrorLog::getErrorLog()->putErrInfo("读取目录项状态失败", filePath);
            success = false;
        }
    }
    closedir(d);
    return success;
}

int ElfUtils::isElfFile(const char *file)
{
    bool ret = false;
    const char elfHead[7] = {0x7f,0x45,0x4c,0x46,0x02,0x01,0x01};
    char targetHead[7] = { 0 };
    int fd = 0;

    fd = open(file, O_RDONLY);
    if(fd < 0)
    {
        ErrorLog::getErrorLog()->putErrInfo("扫描时打开文件失败", file);
        return -1;
    }

    const ssize_t bytes = read(fd,targetHead,sizeof(targetHead));
    if(bytes < 0)
    {
        close(fd);
        ErrorLog::getErrorLog()->putErrInfo("扫描时读取文件失败", file);
        return -1;
    }
    if(static_cast<size_t>(bytes) == sizeof(targetHead))
        if(memcmp(elfHead,targetHead,sizeof(targetHead)) == 0) ret = true;
    close(fd);
    return ret;
}

bool ElfUtils::isDirectory(const std::string& path)
{
    struct stat sb;
    return stat(path.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode);
}
