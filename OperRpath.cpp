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
OperRpath::RpathState OperRpath::rpathState(const string& path) const
{
    ElfPtrs<Traits> ptrs(ElfOpenMode::ReadOnly);
    if (!ptrs.initPtrs(path.c_str())) return RpathState::Invalid;
    if (!ptrs.findProgramHeader(PT_DYNAMIC)) return RpathState::NoDynamic;
    RpathEntry<Traits> entry;
    return resolveEntry(ptrs, entry) ? RpathState::HasEntry : RpathState::NoEntry;
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
bool OperRpath::buildDynamicWithRpath(const string& oldDynamic,
                                      typename Traits::Xword newStringOffset,
                                      string& newDynamic) const
{
    using Dyn = typename Traits::Dyn;
    if (oldDynamic.size() % sizeof(Dyn) != 0) return false;
    const size_t count = oldDynamic.size() / sizeof(Dyn);
    if (count == 0) return false;
    const Dyn* dyn = reinterpret_cast<const Dyn*>(oldDynamic.data());
    if (dyn[count - 1].d_tag != DT_NULL) return false;
    for (size_t i = 0; i < count; ++i)
    {
        if (dyn[i].d_tag == DT_NULL) break;
        // DT_MIPS_RLD_MAP_REL 的值与自身槽位地址相关，随 .dynamic 搬家会错位。
        if (dyn[i].d_tag == DT_MIPS_RLD_MAP_REL) return false;
    }
    // 新条目放最前面，其余条目下移一位，DT_NULL 仍在末尾。
    newDynamic.assign(oldDynamic.size() + sizeof(Dyn), '\0');
    Dyn entry = {};
    entry.d_tag = DT_RUNPATH;
    entry.d_un.d_val = newStringOffset;
    memcpy(&newDynamic[0], &entry, sizeof(Dyn));
    memcpy(&newDynamic[sizeof(Dyn)], oldDynamic.data(), oldDynamic.size());
    return true;
}

template <typename Traits>
bool OperRpath::rewriteWithRpath(const string& path, const string& newRpath,
                                 const RpathEntry<Traits>* entry)
{
    using Word = typename Traits::Word;
    using Xword = typename Traits::Xword;
    // 读旧 .dynstr 与 .dynamic 内容（只读）。
    string oldDynstr;
    string oldDynamic;
    {
        ElfPtrs<Traits> inspect(ElfOpenMode::ReadOnly);
        if (!inspect.initPtrs(path.c_str()))
        {
            ErrorLog::getErrorLog()->putErrInfo("initTargetPtrs失败", path);
            return false;
        }
        TableInfo<Traits>* tb_dynstr = inspect.getTableInfo(".dynstr");
        if (!tb_dynstr)
        {
            ErrorLog::getErrorLog()->putErrInfo("目标没有 .dynstr", path);
            return false;
        }
        const char* base = reinterpret_cast<const char*>(inspect.elf_hdr);
        oldDynstr.assign(base + tb_dynstr->addr->sh_offset, tb_dynstr->addr->sh_size);
        typename Traits::Phdr* dynamic = inspect.findProgramHeader(PT_DYNAMIC);
        if (!dynamic)
        {
            ErrorLog::getErrorLog()->putErrInfo("目标没有动态段，无法设置 RPATH", path);
            return false;
        }
        oldDynamic.assign(base + dynamic->p_offset, dynamic->p_filesz);
    }
    // 新串追加到 .dynstr 末尾：旧串偏移全部保留。
    const Xword newStringOffset = static_cast<Xword>(oldDynstr.size());
    string newDynstr = oldDynstr;
    newDynstr += newRpath;
    newDynstr += '\0';

    vector<typename ElfRewriter<Traits>::SectionReplacement> replacements;
    replacements.push_back({".dynstr", newDynstr});
    map<Word, Xword> dynamicUpdates;
    if (entry)
    {
        // 有 RPATH 条目：.dynamic 不动，只改该条目的 d_val。
        dynamicUpdates[entry->tag] = newStringOffset;
    }
    else
    {
        // 无 RPATH 条目：.dynamic 也要搬家并插入新 DT_RUNPATH。
        string newDynamic;
        if (!buildDynamicWithRpath<Traits>(oldDynamic, newStringOffset, newDynamic))
        {
            ErrorLog::getErrorLog()->putErrInfo("无法在 .dynamic 中新增 RPATH 条目（可能含位置敏感条目）", path);
            return false;
        }
        replacements.push_back({".dynamic", newDynamic});
    }
    if (!ElfRewriter<Traits>::rewriteFile(path, replacements, dynamicUpdates))
        return false;
    return verifyRpath<Traits>(path, newRpath);
}

template <typename Traits>
bool OperRpath::setRpathForFile(const string& path, const string& newRpath, bool dryRun)
{
    RpathEntry<Traits> entry;
    bool hasEntry = false;
    {
        ElfPtrs<Traits> inspect(ElfOpenMode::ReadOnly);
        if (!inspect.initPtrs(path.c_str()))
        {
            ErrorLog::getErrorLog()->putErrInfo("initTargetPtrs失败", path);
            return false;
        }
        if (!inspect.findProgramHeader(PT_DYNAMIC))
        {
            ErrorLog::getErrorLog()->putErrInfo("目标没有动态段，无法设置 RPATH", path);
            return false;
        }
        hasEntry = resolveEntry(inspect, entry);
    }
    if (hasEntry && newRpath == entry.value)
    {
        cout << "  未改动（RPATH 已是目标值）: " << entry.value << "  文件:" << path << endl;
        return true;
    }
    if (!hasEntry && newRpath.empty())
    {
        cout << "  未改动（目标本来就没有 RPATH）: " << path << endl;
        return true;
    }
    if (!validateNewRpath(newRpath))
    {
        ErrorLog::getErrorLog()->putErrInfo("新 RPATH 语法无效：每段须为绝对路径或以 $ 开头（如 $ORIGIN）", path);
        return false;
    }
    // 长度分流：等长或更短原地改；更长或无 RPATH 走文件重写。
    const bool needsRewrite = !hasEntry || newRpath.size() + 1 > entry.value.size() + 1;
    if (dryRun)
    {
        if (hasEntry)
            cout << "  试运行，将修改 " << entry.value << " ----> " << newRpath;
        else
            cout << "  试运行，将新增 RPATH " << newRpath;
        if (needsRewrite) cout << "（需重写文件）";
        cout << "  文件:" << path << endl;
        return true;
    }
    if (!needsRewrite)
    {
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
    const RpathEntry<Traits>* entryPtr = hasEntry ? &entry : nullptr;
    const bool ok = rewriteWithRpath<Traits>(path, newRpath, entryPtr);
    if (ok)
    {
        if (hasEntry)
            printf("  修改 RPATH（重写文件）: %-27s ----> %-27s  文件:%s\n",
                   entry.value.c_str(), newRpath.c_str(), path.c_str());
        else
            printf("  新增 RPATH（重写文件）: %s  文件:%s\n", newRpath.c_str(), path.c_str());
    }
    return ok;
}

template <typename Traits>
FileAdaptResult OperRpath::adaptOneFileFor(const string& path, const string& newRpath, bool dryRun)
{
    const RpathState state = rpathState<Traits>(path);
    if (state == RpathState::Invalid)
    {
        ErrorLog::getErrorLog()->putErrInfo("目标不是有效的 ELF 文件", path);
        return FileAdaptResult::Failed;
    }
    if (state == RpathState::NoDynamic)
    {
        cout << "  跳过（无动态段，不适用 RPATH）: " << path << endl;
        return FileAdaptResult::Skipped;
    }
    return setRpathForFile<Traits>(path, newRpath, dryRun)
        ? FileAdaptResult::Patched : FileAdaptResult::Failed;
}

template <typename Traits>
FileAdaptResult OperRpath::showOneFileFor(const string& path) const
{
    const RpathState state = rpathState<Traits>(path);
    if (state == RpathState::Invalid) return FileAdaptResult::Failed;
    if (state != RpathState::HasEntry)
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
            ErrorLog::getErrorLog()->putErrInfo("目录中没有可修改 RPATH 的动态文件", path);
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
