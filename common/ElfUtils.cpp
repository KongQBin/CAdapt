#include "ElfUtils.h"
#include "ElfPtrs.h"
#include <elf.h>
#include <iostream>
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
    // 只认魔数 + 类别（32/64 位）+ 字节序（小端/大端）+ 版本；大端文件
    // 也收集，由各适配器识别后警告并跳过，而不是在扫描时悄悄丢掉。
    if(static_cast<size_t>(bytes) == sizeof(targetHead) &&
        memcmp(targetHead, ELFMAG, SELFMAG) == 0 &&
        (targetHead[EI_CLASS] == ELFCLASS32 || targetHead[EI_CLASS] == ELFCLASS64) &&
        (targetHead[EI_DATA] == ELFDATA2LSB || targetHead[EI_DATA] == ELFDATA2MSB) &&
        targetHead[EI_VERSION] == EV_CURRENT)
        ret = true;
    close(fd);
    return ret;
}

bool ElfUtils::isDirectory(const std::string& path)
{
    struct stat sb;
    return stat(path.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode);
}

bool ElfUtils::identifyElf(const std::string& path, ElfIdentity& out)
{
    // 文件头前 20 字节：e_ident(16) + e_version(2) + e_machine(2)，
    // e_machine 在偏移 18 处，两种类别布局相同。
    unsigned char head[20] = { 0 };
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    ssize_t got = 0;
    while (got < static_cast<ssize_t>(sizeof(head)))
    {
        const ssize_t n = read(fd, head + got, sizeof(head) - static_cast<size_t>(got));
        if (n < 0)
        {
            if (errno == EINTR) continue;
            close(fd);
            return false;
        }
        if (n == 0) break;
        got += n;
    }
    close(fd);
    if (got < static_cast<ssize_t>(sizeof(head)) || memcmp(head, ELFMAG, SELFMAG) != 0)
        return false;
    if (head[EI_CLASS] != ELFCLASS32 && head[EI_CLASS] != ELFCLASS64) return false;
    if (head[EI_DATA] != ELFDATA2LSB && head[EI_DATA] != ELFDATA2MSB) return false;
    out.elfClass = head[EI_CLASS];
    out.elfData = head[EI_DATA];
    // e_machine 按文件字节序还原；识别本身不依赖本机字节序。
    out.machine = out.elfData == ELFDATA2LSB
        ? static_cast<unsigned short>(head[18] | (head[19] << 8))
        : static_cast<unsigned short>((head[18] << 8) | head[19]);
    return true;
}

std::string ElfUtils::machineName(unsigned short machine)
{
    switch (machine)
    {
    case EM_386: return "i386";
    case EM_X86_64: return "x86-64";
    case EM_ARM: return "ARM";
    case EM_AARCH64: return "AArch64";
    case EM_RISCV: return "RISC-V";
    case EM_PPC: return "PowerPC";
    case EM_PPC64: return "PowerPC64";
    case EM_MIPS: return "MIPS";
    case EM_S390: return "s390x";
    case EM_IA_64: return "IA-64";
    case EM_SPARC: return "SPARC";
    case EM_SPARCV9: return "SPARC V9";
    default: return "未知架构";
    }
}

std::string ElfUtils::describeIdentity(const ElfIdentity& id)
{
    std::string result;
    if (id.elfClass == ELFCLASS32) result = "32 位";
    else if (id.elfClass == ELFCLASS64) result = "64 位";
    else return "未知 ELF";
    result += id.elfData == ELFDATA2LSB ? "小端"
        : (id.elfData == ELFDATA2MSB ? "大端" : "未知字节序");
    result += " ";
    result += machineName(id.machine);
    return result;
}

void ElfUtils::warnUnsupportedEndian(const std::string& path, const ElfIdentity& id)
{
    const std::string message = "警告：大端 ELF 文件暂不支持，跳过：" + path +
        "（" + describeIdentity(id) + "）";
    std::cerr << message << std::endl;
    ErrorLog::getErrorLog()->putErrInfo(message, path);
}

void ElfUtils::logIdentifyFailure(const std::string& path)
{
    // 仅用于记录精确原因：打开失败、不是普通文件、文件头无效等。
    // 类别用 64 位探测即可——能识别的文件不会走到这里。
    ElfPtrs<Elf64Traits> probe(ElfOpenMode::ReadOnly);
    probe.initPtrs(path);
}
