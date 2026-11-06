#include "OperInterp.h"
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>

using namespace std;

bool OperInterp::validateNewInterpreter(const string& newInterp, Elf64_Half targetMachine) const
{
    if (newInterp.empty() || newInterp[0] != '/')
    {
        ErrorLog::getErrorLog()->putErrInfo("新解释器必须是绝对路径", newInterp);
        return false;
    }
    if (newInterp.find('\0') != string::npos)
    {
        ErrorLog::getErrorLog()->putErrInfo("新解释器路径含非法字符", newInterp);
        return false;
    }
    struct stat st;
    if (stat(newInterp.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
    {
        ErrorLog::getErrorLog()->putErrInfo("新解释器不存在或不是普通文件", newInterp);
        return false;
    }
    if (access(newInterp.c_str(), X_OK) != 0)
    {
        ErrorLog::getErrorLog()->putErrInfo("新解释器不可执行", newInterp);
        return false;
    }

    ElfPtrs loader(ElfOpenMode::ReadOnly);
    if (!loader.initPtrs(newInterp))
    {
        ErrorLog::getErrorLog()->putErrInfo("新解释器不是有效的64位小端ELF", newInterp);
        return false;
    }
    if (loader.elf_hdr->e_type != ET_DYN)
    {
        ErrorLog::getErrorLog()->putErrInfo("新解释器不是动态链接器（需要 ET_DYN）", newInterp);
        return false;
    }
    if (loader.elf_hdr->e_machine != targetMachine)
    {
        ErrorLog::getErrorLog()->putErrInfo("新解释器与目标机器架构不一致", newInterp);
        return false;
    }
    if (loader.findProgramHeader(PT_INTERP) != nullptr)
    {
        ErrorLog::getErrorLog()->putErrInfo("新解释器本身带有 PT_INTERP，不是独立的动态链接器", newInterp);
        return false;
    }
    return true;
}

int OperInterp::hasInterpreter(const string& path) const
{
    ElfPtrs ptrs(ElfOpenMode::ReadOnly);
    if (!ptrs.initPtrs(path)) return -1;
    return ptrs.findProgramHeader(PT_INTERP) != nullptr ? 1 : 0;
}

bool OperInterp::showInterpreterForFile(const string& path) const
{
    ElfPtrs ptrs(ElfOpenMode::ReadOnly);
    if (!ptrs.initPtrs(path))
    {
        ErrorLog::getErrorLog()->putErrInfo("读取目标失败", path);
        return false;
    }
    Elf64_Phdr* interp = ptrs.findProgramHeader(PT_INTERP);
    if (!interp)
    {
        ErrorLog::getErrorLog()->putErrInfo("目标没有 PT_INTERP", path);
        return false;
    }
    const char* base = reinterpret_cast<const char*>(ptrs.elf_hdr);
    cout << path << ": " << (base + interp->p_offset) << endl;
    return true;
}

bool OperInterp::showTargets(const string& path)
{
    DIR *dir = opendir(path.c_str());
    if (dir)
    {
        closedir(dir);
        targetElf_vct.clear();
        if (!getAllElf(path.c_str())) return false;
        bool success = true;
        unsigned printed = 0;
        for (const auto& file : targetElf_vct)
        {
            const int has = hasInterpreter(file);
            if (has < 0) success = false;
            else if (has == 0) cout << "  跳过（无 PT_INTERP）: " << file << endl;
            else if (showInterpreterForFile(file)) ++printed;
            else success = false;
        }
        return success && printed > 0;
    }
    return showInterpreterForFile(path);
}

bool OperInterp::setInterpreterForFile(const string& path, const string& newInterp, bool dryRun)
{
    unique_ptr<ElfPtrs> targetPtrs(new ElfPtrs(ElfOpenMode::ReadWrite));
    if (!targetPtrs->initPtrs(path.c_str()))
    {
        ErrorLog::getErrorLog()->putErrInfo("initTargetPtrs失败", path);
        return false;
    }
    if (targetPtrs->elf_hdr->e_type == ET_REL)
    {
        ErrorLog::getErrorLog()->putErrInfo("目标没有 PT_INTERP", path);
        return false;
    }
    Elf64_Phdr* interp = targetPtrs->findProgramHeader(PT_INTERP);
    if (!interp)
    {
        ErrorLog::getErrorLog()->putErrInfo("目标没有 PT_INTERP", path);
        return false;
    }

    char* base = reinterpret_cast<char*>(targetPtrs->elf_hdr);
    const char* oldInterp = base + interp->p_offset;
    // initPtrs 已校验 PT_INTERP 在文件内以 NUL 结尾，此处长度可信。
    // 先拷一份旧值：下面 memcpy 会覆盖同一块内存，之后不能再读 oldInterp。
    const string oldCopy(oldInterp);
    const size_t oldLen = oldCopy.size();
    const size_t newLen = newInterp.size();

    // 第一阶段硬限制：新路径（含结尾 NUL）必须放进原段，剩余字节补零。
    if (newLen + 1 > interp->p_filesz)
    {
        ErrorLog::getErrorLog()->putErrInfo(
            string("新解释器过长，第一阶段只支持等长或更短：新长度 ") + to_string(newLen) +
            "，原段容量 " + to_string(interp->p_filesz - 1), path);
        return false;
    }
    if (newInterp == oldCopy)
    {
        cout << "  未改动（解释器已是目标值）: " << oldCopy << "  文件:" << path << endl;
        return true;
    }
    // 任何写入之前先校验新解释器；校验失败时目标保持不变。
    if (!validateNewInterpreter(newInterp, targetPtrs->elf_hdr->e_machine)) return false;

    if (dryRun)
    {
        cout << "  试运行，将修改 " << oldCopy << " ----> " << newInterp
             << "  文件:" << path << endl;
        return true;
    }

    char* interpStr = base + interp->p_offset;
    memcpy(interpStr, newInterp.c_str(), newLen + 1);
    if (newLen + 1 < interp->p_filesz)
        memset(interpStr + newLen + 1, 0, interp->p_filesz - (newLen + 1));

    // .interp 节区若存在且与程序头不在同一文件偏移，也同步改写，保持两者一致。
    TableInfo* tb_interp = targetPtrs->getTableInfo(".interp");
    if (tb_interp && tb_interp->addr->sh_offset != interp->p_offset)
    {
        char* sectionStr = base + tb_interp->addr->sh_offset;
        if (strncmp(sectionStr, oldCopy.c_str(), oldLen + 1) == 0 &&
            tb_interp->addr->sh_size >= newLen + 1)
        {
            memcpy(sectionStr, newInterp.c_str(), newLen + 1);
            if (newLen + 1 < tb_interp->addr->sh_size)
                memset(sectionStr + newLen + 1, 0, tb_interp->addr->sh_size - (newLen + 1));
        }
        else
        {
            cerr << "警告：.interp 节区内容与 PT_INTERP 不一致，只修改了 PT_INTERP，文件:" << path << endl;
        }
    }

    printf("  修改解释器: %-27s ----> %-27s  文件:%s\n", oldCopy.c_str(), newInterp.c_str(), path.c_str());
    if (!targetPtrs->flush()) return false;

    // 写盘后重读验证：确认磁盘上的值确实是新解释器。
    ElfPtrs verify(ElfOpenMode::ReadOnly);
    if (!verify.initPtrs(path))
    {
        ErrorLog::getErrorLog()->putErrInfo("写入后重读验证失败", path);
        return false;
    }
    Elf64_Phdr* verified = verify.findProgramHeader(PT_INTERP);
    if (!verified || string(reinterpret_cast<const char*>(verify.elf_hdr) + verified->p_offset) != newInterp)
    {
        ErrorLog::getErrorLog()->putErrInfo("写入后验证：解释器与预期不符", path);
        return false;
    }
    return true;
}

bool OperInterp::adaptTargets(const string& path, const string& newInterp, bool dryRun)
{
    DIR *dir = opendir(path.c_str());
    if (dir)
    {
        closedir(dir);
        targetElf_vct.clear();
        if (!getAllElf(path.c_str())) return false;

        unsigned patched = 0;
        unsigned skipped = 0;
        bool success = true;
        for (const auto& file : targetElf_vct)
        {
            const int has = hasInterpreter(file);
            if (has < 0)
            {
                ErrorLog::getErrorLog()->putErrInfo("目录中有无效 ELF 文件", file);
                success = false;
                continue;
            }
            if (has == 0)
            {
                cout << "  跳过（无 PT_INTERP）: " << file << endl;
                ++skipped;
                continue;
            }
            if (setInterpreterForFile(file, newInterp, dryRun)) ++patched;
            else success = false;
        }
        if (patched == 0)
        {
            ErrorLog::getErrorLog()->putErrInfo("目录中没有带 PT_INTERP 的可执行文件", path);
            return false;
        }
        (void)skipped;
        return success;
    }
    return setInterpreterForFile(path, newInterp, dryRun);
}

bool OperInterp::getAllElf(const char *dir)
{
    DIR *d;
    struct dirent *file;
    struct stat sb;
    string filePath;

    if(!(d = opendir(dir)))
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
        filePath = string(dir) + "/" + file->d_name;
        if(lstat(filePath.c_str(), &sb) >= 0)
        {
            if(S_ISLNK(sb.st_mode)) continue;
            if(S_ISDIR(sb.st_mode))
            {
                if (!getAllElf(filePath.c_str())) success = false;
            }
            else if(S_ISREG(sb.st_mode))
            {
                const int result = isElf(filePath.c_str());
                if (result < 0) success = false;
                else if (result > 0)
                    targetElf_vct.push_back(filePath);
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

int OperInterp::isElf(const char *file) const
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
