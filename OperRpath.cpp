#include "OperRpath.h"
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

using namespace std;

bool OperRpath::resolveEntries(ElfPtrs& ptrs, vector<RpathEntry>& entries) const
{
    entries.clear();
    Elf64_Phdr* dynamic = ptrs.findProgramHeader(PT_DYNAMIC);
    if (!dynamic) return false;
    const char* base = reinterpret_cast<const char*>(ptrs.elf_hdr);
    const Elf64_Dyn* dyn = reinterpret_cast<const Elf64_Dyn*>(base + dynamic->p_offset);
    const size_t count = dynamic->p_filesz / sizeof(Elf64_Dyn);
    const Elf64_Dyn* runpath = nullptr;
    const Elf64_Dyn* rpath = nullptr;
    for (size_t i = 0; i < count; ++i)
    {
        if (dyn[i].d_tag == DT_RUNPATH) runpath = &dyn[i];
        else if (dyn[i].d_tag == DT_RPATH) rpath = &dyn[i];
        else if (dyn[i].d_tag == DT_NULL) break;
    }
    if (!runpath && !rpath) return false;
    TableInfo* tb_dynstr = ptrs.getTableInfo(".dynstr");
    if (!tb_dynstr) return false;
    // RUNPATH 在前：它是优先改的那一项。
    const Elf64_Dyn* chosen[2] = { runpath, rpath };
    for (int k = 0; k < 2; ++k)
    {
        const Elf64_Dyn* item = chosen[k];
        if (!item) continue;
        const Elf64_Xword strOff = item->d_un.d_val;
        if (strOff >= tb_dynstr->addr->sh_size) return false;
        const char* str = base + tb_dynstr->addr->sh_offset + strOff;
        const size_t maxLen = tb_dynstr->addr->sh_size - static_cast<size_t>(strOff);
        const char* nul = static_cast<const char*>(memchr(str, '\0', maxLen));
        if (!nul) return false;
        RpathEntry entry;
        entry.tag = static_cast<Elf64_Word>(item->d_tag);
        entry.strOffset = strOff;
        entry.fileOffset = tb_dynstr->addr->sh_offset + strOff;
        entry.value.assign(str, static_cast<size_t>(nul - str));
        entries.push_back(entry);
    }
    return !entries.empty();
}

bool OperRpath::resolveEntry(ElfPtrs& ptrs, RpathEntry& entry) const
{
    vector<RpathEntry> entries;
    if (!resolveEntries(ptrs, entries)) return false;
    entry = entries.front();
    return true;
}

int OperRpath::hasRpath(const string& path) const
{
    ElfPtrs ptrs(ElfOpenMode::ReadOnly);
    if (!ptrs.initPtrs(path.c_str())) return -1;
    RpathEntry entry;
    return resolveEntry(ptrs, entry) ? 1 : 0;
}

bool OperRpath::validateNewRpath(const string& newRpath) const
{
    if (newRpath.empty()) return true; // 空串表示清空。
    size_t start = 0;
    while (true)
    {
        const size_t colon = newRpath.find(':', start);
        const string part = newRpath.substr(start, colon == string::npos ? string::npos : colon - start);
        if (!part.empty() && part[0] != '/' && part[0] != '$') return false;
        if (colon == string::npos) break;
        start = colon + 1;
    }
    return true;
}

bool OperRpath::setRpathForFile(const string& path, const string& newRpath, bool dryRun)
{
    RpathEntry entry;
    {
        ElfPtrs inspect(ElfOpenMode::ReadOnly);
        if (!inspect.initPtrs(path.c_str()))
        {
            ErrorLog::getErrorLog()->putErrInfo("initTargetPtrs失败", path);
            return false;
        }
        if (!resolveEntry(inspect, entry))
        {
            ErrorLog::getErrorLog()->putErrInfo("目标没有 DT_RPATH/DT_RUNPATH", path);
            return false;
        }
    }
    const size_t oldLen = entry.value.size();

    if (newRpath == entry.value)
    {
        cout << "  未改动（RPATH 已是目标值）: " << entry.value << "  文件:" << path << endl;
        return true;
    }
    if (!validateNewRpath(newRpath))
    {
        ErrorLog::getErrorLog()->putErrInfo("新 RPATH 语法无效：每段须为绝对路径或以 $ 开头（如 $ORIGIN）", path);
        return false;
    }
    // 新串更长时无法原地容纳；加载器按 DT_STRTAB + 偏移从已映射内存读
    // 串，追加到文件末尾的串读不到，因此也不能像解释器那样重写。
    if (newRpath.size() + 1 > oldLen + 1)
    {
        ErrorLog::getErrorLog()->putErrInfo(
            string("新 RPATH 过长，只支持等长或更短：新长度 ") + to_string(newRpath.size()) +
            "，原长度 " + to_string(oldLen), path);
        return false;
    }
    if (dryRun)
    {
        cout << "  试运行，将修改 " << entry.value << " ----> " << newRpath
             << "  文件:" << path << endl;
        return true;
    }

    unique_ptr<ElfPtrs> targetPtrs(new ElfPtrs(ElfOpenMode::ReadWrite));
    if (!targetPtrs->initPtrs(path.c_str()))
    {
        ErrorLog::getErrorLog()->putErrInfo("initTargetPtrs失败", path);
        return false;
    }
    RpathEntry live;
    if (!resolveEntry(*targetPtrs, live))
    {
        ErrorLog::getErrorLog()->putErrInfo("目标没有 DT_RPATH/DT_RUNPATH", path);
        return false;
    }
    const char* tagName = live.tag == DT_RUNPATH ? "RUNPATH" : "RPATH";
    char* base = reinterpret_cast<char*>(targetPtrs->elf_hdr);
    char* str = base + live.fileOffset;
    memcpy(str, newRpath.c_str(), newRpath.size() + 1);
    if (newRpath.size() < live.value.size())
        memset(str + newRpath.size() + 1, 0, live.value.size() - newRpath.size());
    printf("  修改 RPATH (%s): %-27s ----> %-27s  文件:%s\n",
           tagName, entry.value.c_str(), newRpath.c_str(), path.c_str());
    if (!targetPtrs->flush()) return false;
    return verifyRpath(path, newRpath);
}

bool OperRpath::verifyRpath(const string& path, const string& expected) const
{
    ElfPtrs verify(ElfOpenMode::ReadOnly);
    if (!verify.initPtrs(path))
    {
        ErrorLog::getErrorLog()->putErrInfo("写入后重读验证失败", path);
        return false;
    }
    RpathEntry entry;
    if (!resolveEntry(verify, entry) || entry.value != expected)
    {
        ErrorLog::getErrorLog()->putErrInfo("写入后验证：RPATH 与预期不符", path);
        return false;
    }
    return true;
}

bool OperRpath::showRpathForFile(const string& path) const
{
    ElfPtrs ptrs(ElfOpenMode::ReadOnly);
    if (!ptrs.initPtrs(path.c_str()))
    {
        ErrorLog::getErrorLog()->putErrInfo("initTargetPtrs失败", path);
        return false;
    }
    vector<RpathEntry> entries;
    if (!resolveEntries(ptrs, entries))
    {
        ErrorLog::getErrorLog()->putErrInfo("目标没有 DT_RPATH/DT_RUNPATH", path);
        return false;
    }
    for (const auto& entry : entries)
    {
        const char* tagName = entry.tag == DT_RUNPATH ? "RUNPATH" : "RPATH";
        cout << path << ": " << tagName << "=" << entry.value << endl;
    }
    return true;
}

bool OperRpath::adaptTargets(const string& path, const string& newRpath, bool dryRun)
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
            const int has = hasRpath(file);
            if (has < 0)
            {
                ErrorLog::getErrorLog()->putErrInfo("目录中有无效 ELF 文件", file);
                success = false;
                continue;
            }
            if (has == 0)
            {
                cout << "  跳过（无 RPATH）: " << file << endl;
                ++skipped;
                continue;
            }
            if (setRpathForFile(file, newRpath, dryRun)) ++patched;
            else success = false;
        }
        if (patched == 0)
        {
            ErrorLog::getErrorLog()->putErrInfo("目录中没有带 RPATH 的可执行文件", path);
            return false;
        }
        (void)skipped;
        return success;
    }
    return setRpathForFile(path, newRpath, dryRun);
}

bool OperRpath::showTargets(const string& path)
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
            const int has = hasRpath(file);
            if (has < 0) success = false;
            else if (has == 0) cout << "  跳过（无 RPATH）: " << file << endl;
            else if (showRpathForFile(file)) ++printed;
            else success = false;
        }
        return success && printed > 0;
    }
    return showRpathForFile(path);
}

bool OperRpath::getAllElf(const char *dir)
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

int OperRpath::isElf(const char *file) const
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
