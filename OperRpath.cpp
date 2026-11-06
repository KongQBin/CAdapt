#include "OperRpath.h"
#include "ElfUtils.h"
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

using namespace std;

template <typename Traits>
bool OperRpath::resolveEntries(ElfPtrs<Traits>& ptrs, vector<RpathEntry<Traits>>& entries) const
{
    entries.clear();
    typename Traits::Phdr* dynamic = ptrs.findProgramHeader(PT_DYNAMIC);
    if (!dynamic) return false;
    const char* base = reinterpret_cast<const char*>(ptrs.elf_hdr);
    const typename Traits::Dyn* dyn = reinterpret_cast<const typename Traits::Dyn*>(base + dynamic->p_offset);
    const size_t count = dynamic->p_filesz / sizeof(typename Traits::Dyn);
    const typename Traits::Dyn* runpath = nullptr;
    const typename Traits::Dyn* rpath = nullptr;
    for (size_t i = 0; i < count; ++i)
    {
        if (dyn[i].d_tag == DT_RUNPATH) runpath = &dyn[i];
        else if (dyn[i].d_tag == DT_RPATH) rpath = &dyn[i];
        else if (dyn[i].d_tag == DT_NULL) break;
    }
    if (!runpath && !rpath) return false;
    TableInfo<Traits>* tb_dynstr = ptrs.getTableInfo(".dynstr");
    if (!tb_dynstr) return false;
    // RUNPATH 在前：它是优先改的那一项。
    const typename Traits::Dyn* chosen[2] = { runpath, rpath };
    for (int k = 0; k < 2; ++k)
    {
        const typename Traits::Dyn* item = chosen[k];
        if (!item) continue;
        const typename Traits::Xword strOff = item->d_un.d_val;
        if (strOff >= tb_dynstr->addr->sh_size) return false;
        const char* str = base + tb_dynstr->addr->sh_offset + strOff;
        const size_t maxLen = tb_dynstr->addr->sh_size - static_cast<size_t>(strOff);
        const char* nul = static_cast<const char*>(memchr(str, '\0', maxLen));
        if (!nul) return false;
        RpathEntry<Traits> entry;
        entry.tag = static_cast<typename Traits::Word>(item->d_tag);
        entry.strOffset = strOff;
        entry.fileOffset = tb_dynstr->addr->sh_offset + strOff;
        entry.value.assign(str, static_cast<size_t>(nul - str));
        entries.push_back(entry);
    }
    return !entries.empty();
}

template <typename Traits>
bool OperRpath::resolveEntry(ElfPtrs<Traits>& ptrs, RpathEntry<Traits>& entry) const
{
    vector<RpathEntry<Traits>> entries;
    if (!resolveEntries(ptrs, entries)) return false;
    entry = entries.front();
    return true;
}

template <typename Traits>
int OperRpath::hasRpath(const string& path) const
{
    ElfPtrs<Traits> ptrs(ElfOpenMode::ReadOnly);
    if (!ptrs.initPtrs(path.c_str())) return -1;
    RpathEntry<Traits> entry;
    return resolveEntry(ptrs, entry) ? 1 : 0;
}

template <typename Traits>
bool OperRpath::verifyRpath(const string& path, const string& expected) const
{
    ElfPtrs<Traits> verify(ElfOpenMode::ReadOnly);
    if (!verify.initPtrs(path))
    {
        ErrorLog::getErrorLog()->putErrInfo("写入后重读验证失败", path);
        return false;
    }
    RpathEntry<Traits> entry;
    if (!resolveEntry(verify, entry) || entry.value != expected)
    {
        ErrorLog::getErrorLog()->putErrInfo("写入后验证：RPATH 与预期不符", path);
        return false;
    }
    return true;
}

template <typename Traits>
bool OperRpath::showRpathForFile(const string& path) const
{
    ElfPtrs<Traits> ptrs(ElfOpenMode::ReadOnly);
    if (!ptrs.initPtrs(path.c_str()))
    {
        ErrorLog::getErrorLog()->putErrInfo("initTargetPtrs失败", path);
        return false;
    }
    vector<RpathEntry<Traits>> entries;
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

template <typename Traits>
bool OperRpath::setRpathForFile(const string& path, const string& newRpath, bool dryRun)
{
    RpathEntry<Traits> entry;
    {
        ElfPtrs<Traits> inspect(ElfOpenMode::ReadOnly);
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

    unique_ptr<ElfPtrs<Traits>> targetPtrs(new ElfPtrs<Traits>(ElfOpenMode::ReadWrite));
    if (!targetPtrs->initPtrs(path.c_str()))
    {
        ErrorLog::getErrorLog()->putErrInfo("initTargetPtrs失败", path);
        return false;
    }
    RpathEntry<Traits> live;
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
    return verifyRpath<Traits>(path, newRpath);
}

template <typename Traits>
FileAdaptResult OperRpath::adaptOneFileFor(const string& path, const string& newRpath, bool dryRun)
{
    const int has = hasRpath<Traits>(path);
    if (has < 0)
    {
        ErrorLog::getErrorLog()->putErrInfo("目标不是有效的 ELF 文件", path);
        return FileAdaptResult::Failed;
    }
    if (has == 0)
    {
        cout << "  跳过（无 RPATH）: " << path << endl;
        return FileAdaptResult::Skipped;
    }
    return setRpathForFile<Traits>(path, newRpath, dryRun)
        ? FileAdaptResult::Patched : FileAdaptResult::Failed;
}

template <typename Traits>
FileAdaptResult OperRpath::showOneFileFor(const string& path) const
{
    const int has = hasRpath<Traits>(path);
    if (has < 0) return FileAdaptResult::Failed;
    if (has == 0)
    {
        cout << "  跳过（无 RPATH）: " << path << endl;
        return FileAdaptResult::Skipped;
    }
    return showRpathForFile<Traits>(path) ? FileAdaptResult::Patched : FileAdaptResult::Failed;
}

FileAdaptResult OperRpath::adaptOneFile(const string& path, const string& newRpath, bool dryRun)
{
    ElfIdentity id;
    if (!ElfUtils::identifyElf(path, id))
    {
        ElfUtils::logIdentifyFailure(path);
        return FileAdaptResult::Failed;
    }
    if (id.elfData == ELFDATA2MSB)
    {
        ElfUtils::warnUnsupportedEndian(path, id);
        return FileAdaptResult::Skipped;
    }
    if (id.elfClass == ELFCLASS64) return adaptOneFileFor<Elf64Traits>(path, newRpath, dryRun);
    return adaptOneFileFor<Elf32Traits>(path, newRpath, dryRun);
}

FileAdaptResult OperRpath::showOneFile(const string& path) const
{
    ElfIdentity id;
    if (!ElfUtils::identifyElf(path, id))
    {
        ElfUtils::logIdentifyFailure(path);
        return FileAdaptResult::Failed;
    }
    if (id.elfData == ELFDATA2MSB)
    {
        ElfUtils::warnUnsupportedEndian(path, id);
        return FileAdaptResult::Skipped;
    }
    if (id.elfClass == ELFCLASS64) return showOneFileFor<Elf64Traits>(path);
    return showOneFileFor<Elf32Traits>(path);
}

bool OperRpath::adaptTargets(const string& path, const string& newRpath, bool dryRun)
{
    if (ElfUtils::isDirectory(path))
    {
        vector<string> files;
        if (!ElfUtils::collectElfFiles(path, files)) return false;

        unsigned patched = 0;
        unsigned skipped = 0;
        bool success = true;
        for (const auto& file : files)
        {
            const FileAdaptResult result = adaptOneFile(file, newRpath, dryRun);
            if (result == FileAdaptResult::Patched) ++patched;
            else if (result == FileAdaptResult::Skipped) ++skipped;
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
    return adaptOneFile(path, newRpath, dryRun) == FileAdaptResult::Patched;
}

bool OperRpath::showTargets(const string& path)
{
    if (ElfUtils::isDirectory(path))
    {
        vector<string> files;
        if (!ElfUtils::collectElfFiles(path, files)) return false;
        bool success = true;
        unsigned printed = 0;
        for (const auto& file : files)
        {
            const FileAdaptResult result = showOneFile(file);
            if (result == FileAdaptResult::Patched) ++printed;
            else if (result == FileAdaptResult::Failed) success = false;
        }
        return success && printed > 0;
    }
    return showOneFile(path) == FileAdaptResult::Patched;
}
