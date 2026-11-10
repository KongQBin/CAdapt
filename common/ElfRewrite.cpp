#include "ElfRewrite.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits>
#include <algorithm>

using namespace std;

namespace {

// 对齐取整（m 为 2 的幂）。
uint64_t roundUp(uint64_t n, uint64_t m)
{
    return (n + m - 1) / m * m;
}

constexpr uint64_t kPageSize = 0x1000;
constexpr uint64_t kMaxSegmentAlignment = 0x1000000; // 16 MiB，与 patchelf 相同。

} // namespace

template <typename Traits>
string ElfRewriter<Traits>::pointerSection(typename Traits::Word tag)
{
    switch (tag)
    {
        case DT_STRTAB: return ".dynstr";
        case DT_SYMTAB: return ".dynsym";
        case DT_HASH: return ".hash";
        case DT_GNU_HASH: return ".gnu.hash";
        case DT_JMPREL: return ".rela.plt"; // 32 位常用 .rel.plt，见下文回退。
        case DT_REL: return ".rel.dyn";
        case DT_RELA: return ".rela.dyn";
        case DT_VERNEED: return ".gnu.version_r";
        case DT_VERDEF: return ".gnu.version_d";
        case DT_VERSYM: return ".gnu.version";
        case DT_INIT_ARRAY: return ".init_array";
        case DT_FINI_ARRAY: return ".fini_array";
        case DT_PREINIT_ARRAY: return ".preinit_array";
        default: return "";
    }
}

template <typename Traits>
bool ElfRewriter<Traits>::fixDynamic(char* base, uint64_t dynamicOffset, size_t count,
                                     const map<typename Traits::Word, typename Traits::Xword>& dynamicUpdates,
                                     const map<string, uint64_t>& newAddrByName,
                                     uint64_t newDynstrSize)
{
    using Dyn = typename Traits::Dyn;
    using Word = typename Traits::Word;
    using Xword = typename Traits::Xword;
    Dyn* dyn = reinterpret_cast<Dyn*>(base + dynamicOffset);
    // 先应用调用方指定的条目更新（如 DT_RUNPATH -> 新串偏移）。
    for (const auto& update : dynamicUpdates)
    {
        bool found = false;
        for (size_t i = 0; i < count; ++i)
        {
            if (dyn[i].d_tag == DT_NULL) break;
            if (static_cast<Word>(dyn[i].d_tag) == update.first)
            {
                dyn[i].d_un.d_val = update.second;
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    // 再重算指向被搬节区的指针项；没被搬的节区保持原值不动。
    for (size_t i = 0; i < count; ++i)
    {
        if (dyn[i].d_tag == DT_NULL) break;
        const Word tag = static_cast<Word>(dyn[i].d_tag);
        if (tag == DT_STRSZ)
        {
            auto it = newAddrByName.find(".dynstr");
            if (it != newAddrByName.end())
                dyn[i].d_un.d_val = static_cast<Xword>(newDynstrSize);
            continue;
        }
        string section = pointerSection(tag);
        if (section.empty()) continue;
        auto it = newAddrByName.find(section);
        if (it == newAddrByName.end() && tag == DT_JMPREL)
        {
            // DT_JMPREL 可能是 .rel.plt（32 位常见）或 .rela.plt。
            it = newAddrByName.find(".rel.plt");
        }
        if (it != newAddrByName.end())
            dyn[i].d_un.d_ptr = static_cast<Xword>(it->second);
    }
    return true;
}

template <typename Traits>
bool ElfRewriter<Traits>::verifyRewritten(const string& tmpPath, const string& path,
                                          const vector<SectionReplacement>& replacements,
                                          uint64_t startOffset, uint64_t startPage,
                                          const map<string, pair<uint64_t, uint64_t>>& newPosByName,
                                          const map<string, uint64_t>& newAddrByName)
{
    using Phdr = typename Traits::Phdr;
    using Dyn = typename Traits::Dyn;
    ElfPtrs<Traits> verify(ElfOpenMode::ReadOnly);
    if (!verify.initPtrs(tmpPath))
    {
        ErrorLog::getErrorLog()->putErrInfo("重写后重读临时文件失败", path);
        return false;
    }
    bool foundNewLoad = false;
    for (unsigned i = 0; i < verify.elf_hdr->e_phnum; ++i)
    {
        const Phdr& seg = verify.ph[i];
        if (seg.p_type != PT_LOAD) continue;
        if (seg.p_align > 1 && seg.p_offset % seg.p_align != seg.p_vaddr % seg.p_align)
        {
            ErrorLog::getErrorLog()->putErrInfo("重写后验证：LOAD 段偏移与虚址不对齐", path);
            return false;
        }
        if (seg.p_offset == startOffset && seg.p_vaddr == startPage)
            foundNewLoad = true;
    }
    if (!foundNewLoad)
    {
        ErrorLog::getErrorLog()->putErrInfo("重写后验证：找不到新的 PT_LOAD 段", path);
        return false;
    }
    for (const auto& rep : replacements)
    {
        auto it = newPosByName.find(rep.name);
        if (it == newPosByName.end()) continue;
        TableInfo<Traits>* tb = verify.getTableInfo(rep.name);
        if (!tb || tb->addr->sh_offset != it->second.first ||
            tb->addr->sh_addr != it->second.second ||
            tb->addr->sh_size != rep.content.size())
        {
            ErrorLog::getErrorLog()->putErrInfo("重写后验证：节区 " + rep.name + " 位置与预期不符", path);
            return false;
        }
    }
    auto dynstrIt = newAddrByName.find(".dynstr");
    if (dynstrIt != newAddrByName.end())
    {
        Phdr* dynamic = verify.findProgramHeader(PT_DYNAMIC);
        if (!dynamic)
        {
            ErrorLog::getErrorLog()->putErrInfo("重写后验证：找不到 PT_DYNAMIC", path);
            return false;
        }
        const char* vbase = reinterpret_cast<const char*>(verify.elf_hdr);
        const Dyn* dyn = reinterpret_cast<const Dyn*>(vbase + dynamic->p_offset);
        const size_t count = dynamic->p_filesz / sizeof(Dyn);
        bool foundStrtab = false;
        for (size_t j = 0; j < count; ++j)
        {
            if (dyn[j].d_tag == DT_NULL) break;
            if (dyn[j].d_tag == DT_STRTAB)
            {
                if (dyn[j].d_un.d_ptr != dynstrIt->second)
                {
                    ErrorLog::getErrorLog()->putErrInfo("重写后验证：DT_STRTAB 未指到新 .dynstr", path);
                    return false;
                }
                foundStrtab = true;
                break;
            }
        }
        if (!foundStrtab)
        {
            ErrorLog::getErrorLog()->putErrInfo("重写后验证：.dynamic 里没有 DT_STRTAB", path);
            return false;
        }
    }
    return true;
}

template <typename Traits>
bool ElfRewriter<Traits>::rewriteFile(
    const string& path,
    const vector<SectionReplacement>& replacements,
    const map<typename Traits::Word, typename Traits::Xword>& dynamicUpdates)
{
    using Phdr = typename Traits::Phdr;
    using Dyn = typename Traits::Dyn;
    using Addr = typename Traits::Addr;
    using Off = typename Traits::Off;
    using Xword = typename Traits::Xword;
    using Word = typename Traits::Word;

    if (replacements.empty())
    {
        ErrorLog::getErrorLog()->putErrInfo("重写没有指定要替换的节区", path);
        return false;
    }
    // 重写是 rename 替换，会换 inode：符号链接目标会被替换成本体，必须拒绝。
    struct stat st;
    if (lstat(path.c_str(), &st) != 0)
    {
        ErrorLog::getErrorLog()->putErrInfo("获取目标状态失败", path);
        return false;
    }
    if (S_ISLNK(st.st_mode))
    {
        ErrorLog::getErrorLog()->putErrInfo("文件重写需要替换文件，不支持符号链接目标", path);
        return false;
    }
    if (!S_ISREG(st.st_mode))
    {
        ErrorLog::getErrorLog()->putErrInfo("目标不是普通文件", path);
        return false;
    }
    const uint64_t fileSize = static_cast<uint64_t>(st.st_size);

    // 1. 只读校验并收集布局信息（只存数值，不存指向原映射的指针）。
    ElfPtrs<Traits> probe(ElfOpenMode::ReadOnly);
    if (!probe.initPtrs(path)) return false;
    if (probe.elf_hdr->e_type != ET_EXEC && probe.elf_hdr->e_type != ET_DYN)
    {
        ErrorLog::getErrorLog()->putErrInfo("文件重写只支持可执行文件与动态库", path);
        return false;
    }
    if (!probe.ph)
    {
        ErrorLog::getErrorLog()->putErrInfo("目标没有程序头表，无法新增 LOAD 段", path);
        return false;
    }
    struct ReplacedLayout
    {
        uint64_t newOff = 0;
        uint64_t newAddr = 0;
    };
    vector<ReplacedLayout> layouts(replacements.size());
    for (size_t i = 0; i < replacements.size(); ++i)
    {
        TableInfo<Traits>* tb = probe.getTableInfo(replacements[i].name);
        if (!tb)
        {
            ErrorLog::getErrorLog()->putErrInfo("目标没有节区 " + replacements[i].name, path);
            return false;
        }
        if (tb->addr->sh_type == SHT_NOBITS || !(tb->addr->sh_flags & SHF_ALLOC))
        {
            ErrorLog::getErrorLog()->putErrInfo("节区 " + replacements[i].name + " 不可搬到新 LOAD 段（非分配节区）", path);
            return false;
        }
        if (replacements[i].content.empty())
        {
            ErrorLog::getErrorLog()->putErrInfo("替换内容为空：节区 " + replacements[i].name, path);
            return false;
        }
    }
    // .dynamic 的原位置（指针重算时用；若 .dynamic 本身被搬则用新位置）。
    uint64_t dynamicFileOffset = 0;
    size_t dynamicCount = 0;
    bool dynamicMoves = false;
    for (const auto& rep : replacements)
        if (rep.name == ".dynamic") dynamicMoves = true;
    {
        Phdr* dynamic = probe.findProgramHeader(PT_DYNAMIC);
        if (dynamic)
        {
            dynamicFileOffset = dynamic->p_offset;
            dynamicCount = dynamic->p_filesz / sizeof(Dyn);
        }
    }

    // 2. 算新段位置：文件末尾（按对齐取整）+ 所有 LOAD 段最高虚址之上。
    uint64_t align = kPageSize;
    uint64_t maxVaddrEnd = 0;
    int nullIndex = -1;
    bool hasPhdr = false;
    for (unsigned i = 0; i < probe.elf_hdr->e_phnum; ++i)
    {
        const Phdr& seg = probe.ph[i];
        if (seg.p_type == PT_LOAD)
        {
            align = max(align, static_cast<uint64_t>(seg.p_align));
            maxVaddrEnd = max(maxVaddrEnd, static_cast<uint64_t>(seg.p_vaddr) + seg.p_memsz);
        }
        else if (seg.p_type == PT_NULL && nullIndex < 0)
            nullIndex = static_cast<int>(i);
        else if (seg.p_type == PT_PHDR)
            hasPhdr = true;
    }
    if (align > kMaxSegmentAlignment)
    {
        ErrorLog::getErrorLog()->putErrInfo("段对齐过大，拒绝重写", path);
        return false;
    }
    const uint64_t startOffset = roundUp(fileSize, align);
    const uint64_t startPage = roundUp(maxVaddrEnd, align);
    const bool needPhdrEntry = probe.elf_hdr->e_type == ET_EXEC && !hasPhdr;
    const size_t extraPhdrs = 1 + (needPhdrEntry ? 1 : 0);
    bool relocatePht = nullIndex < 0;
    if (nullIndex >= 0 && needPhdrEntry)
    {
        // 需要两个新项，只有一个空槽，改走搬迁。
        relocatePht = true;
        nullIndex = -1;
    }
    const size_t newPhdrCount = relocatePht
        ? probe.elf_hdr->e_phnum + extraPhdrs
        : probe.elf_hdr->e_phnum;

    // 3. 新段内布局：[程序头表（搬迁时）][被替换节区各一块]。
    const uint64_t sectionAlignment = alignof(Addr);
    uint64_t cur = startOffset;
    uint64_t phtOff = 0;
    uint64_t phtSize = 0;
    if (relocatePht)
    {
        phtOff = cur;
        phtSize = static_cast<uint64_t>(newPhdrCount) * sizeof(Phdr);
        cur += roundUp(phtSize, sectionAlignment);
    }
    map<string, uint64_t> newAddrByName;
    uint64_t newDynstrSize = 0;
    for (size_t i = 0; i < replacements.size(); ++i)
    {
        const uint64_t off = roundUp(cur, sectionAlignment);
        layouts[i].newOff = off;
        layouts[i].newAddr = startPage + (off - startOffset);
        newAddrByName[replacements[i].name] = layouts[i].newAddr;
        if (replacements[i].name == ".dynstr")
            newDynstrSize = replacements[i].content.size();
        cur = off + roundUp(replacements[i].content.size(), sectionAlignment);
    }
    const uint64_t neededSpace = cur - startOffset;
    const uint64_t newSize = startOffset + neededSpace + 1; // +1：旧版 binutils readelf 的 quirk。
    const uint64_t maxAddr = Traits::elfClass == ELFCLASS64
        ? numeric_limits<uint64_t>::max()
        : numeric_limits<uint32_t>::max();
    if (startPage > maxAddr || neededSpace > maxAddr - startPage ||
        startOffset > numeric_limits<Off>::max() || newSize > numeric_limits<Off>::max())
    {
        ErrorLog::getErrorLog()->putErrInfo("重写后的文件或虚址超出目标地址空间", path);
        return false;
    }

    // 4. 新程序头表（内存里先算好）：原表 + 新 PT_LOAD（+ 可能补的 PT_PHDR）。
    vector<Phdr> newPhdrs;
    for (unsigned i = 0; i < probe.elf_hdr->e_phnum; ++i)
        newPhdrs.push_back(probe.ph[i]);
    Phdr load = {};
    load.p_type = PT_LOAD;
    load.p_offset = static_cast<Off>(startOffset);
    load.p_vaddr = static_cast<Addr>(startPage);
    load.p_paddr = static_cast<Addr>(startPage);
    load.p_filesz = static_cast<Xword>(neededSpace);
    load.p_memsz = static_cast<Xword>(neededSpace);
    load.p_flags = PF_R | PF_W;
    load.p_align = static_cast<Xword>(align);
    if (relocatePht)
    {
        newPhdrs.push_back(load);
        if (needPhdrEntry)
        {
            Phdr phdrSeg = {};
            phdrSeg.p_type = PT_PHDR;
            phdrSeg.p_offset = static_cast<Off>(phtOff);
            phdrSeg.p_vaddr = static_cast<Addr>(startPage);
            phdrSeg.p_paddr = static_cast<Addr>(startPage);
            phdrSeg.p_filesz = static_cast<Xword>(newPhdrCount * sizeof(Phdr));
            phdrSeg.p_memsz = static_cast<Xword>(newPhdrCount * sizeof(Phdr));
            phdrSeg.p_flags = PF_R;
            phdrSeg.p_align = alignof(Phdr);
            newPhdrs.push_back(phdrSeg);
        }
        else if (hasPhdr)
        {
            for (auto& phdr : newPhdrs)
            {
                if (phdr.p_type != PT_PHDR) continue;
                phdr.p_offset = static_cast<Off>(phtOff);
                phdr.p_vaddr = static_cast<Addr>(startPage);
                phdr.p_paddr = static_cast<Addr>(startPage);
                phdr.p_filesz = static_cast<Xword>(newPhdrCount * sizeof(Phdr));
                phdr.p_memsz = static_cast<Xword>(newPhdrCount * sizeof(Phdr));
            }
        }
    }
    else
    {
        newPhdrs[static_cast<size_t>(nullIndex)] = load;
    }
    // 被搬节区对应的程序头同步：PT_DYNAMIC / PT_INTERP 跟着节区走。
    for (size_t i = 0; i < replacements.size(); ++i)
    {
        Word type = 0;
        if (replacements[i].name == ".dynamic") type = PT_DYNAMIC;
        else if (replacements[i].name == ".interp") type = PT_INTERP;
        else continue;
        for (auto& phdr : newPhdrs)
        {
            if (phdr.p_type != type) continue;
            phdr.p_offset = static_cast<Off>(layouts[i].newOff);
            phdr.p_vaddr = static_cast<Addr>(layouts[i].newAddr);
            phdr.p_paddr = static_cast<Addr>(layouts[i].newAddr);
            phdr.p_filesz = static_cast<Xword>(replacements[i].content.size());
            phdr.p_memsz = static_cast<Xword>(replacements[i].content.size());
        }
    }
    // .dynamic 的最终文件偏移（指针重算时用）。
    uint64_t dynamicFinalOffset = dynamicFileOffset;
    if (dynamicMoves)
    {
        for (size_t i = 0; i < replacements.size(); ++i)
            if (replacements[i].name == ".dynamic") dynamicFinalOffset = layouts[i].newOff;
    }
    if (!dynamicUpdates.empty() && dynamicFileOffset == 0 && !dynamicMoves)
    {
        ErrorLog::getErrorLog()->putErrInfo("目标没有 PT_DYNAMIC，无法更新 .dynamic 条目", path);
        return false;
    }

    // 5. 复制到同目录临时文件并扩容；rename 之前任何失败都删临时文件，原文件不动。
    TempFileGuard tmp;
    tmp.path = path + ".cadapt-XXXXXX";
    tmp.fd = mkstemp(&tmp.path[0]);
    if (tmp.fd < 0)
    {
        ErrorLog::getErrorLog()->putErrInfo("创建临时文件失败", path);
        return false;
    }
    const int srcFd = open(path.c_str(), O_RDONLY);
    if (srcFd < 0 || !ElfUtils::copyFileTo(srcFd, tmp.fd))
    {
        if (srcFd >= 0) close(srcFd);
        ErrorLog::getErrorLog()->putErrInfo("复制原文件失败", path);
        return false;
    }
    close(srcFd);
    if (ftruncate(tmp.fd, static_cast<off_t>(newSize)) != 0)
    {
        ErrorLog::getErrorLog()->putErrInfo("扩展临时文件失败", path);
        return false;
    }
    if (fchmod(tmp.fd, st.st_mode & 07777) != 0)
    {
        ErrorLog::getErrorLog()->putErrInfo("设置临时文件权限失败", path);
        return false;
    }
    const int chownRc = fchown(tmp.fd, st.st_uid, st.st_gid);
    (void)chownRc;
    const struct timespec times[2] = { st.st_atim, st.st_mtim };
    const int utimeRc = futimens(tmp.fd, times);
    (void)utimeRc;
    close(tmp.fd);
    tmp.fd = -1;

    // 6. 在临时文件上改：写新节区内容、更新节区头、写程序头表、修 .dynamic。
    {
        unique_ptr<ElfPtrs<Traits>> tmpPtrs(new ElfPtrs<Traits>(ElfOpenMode::ReadWrite));
        if (!tmpPtrs->initPtrs(tmp.path))
        {
            ErrorLog::getErrorLog()->putErrInfo("初始化临时文件失败", path);
            return false;
        }
        char* base = reinterpret_cast<char*>(tmpPtrs->elf_hdr);
        for (size_t i = 0; i < replacements.size(); ++i)
        {
            memcpy(base + layouts[i].newOff, replacements[i].content.data(), replacements[i].content.size());
            TableInfo<Traits>* tb = tmpPtrs->getTableInfo(replacements[i].name);
            if (!tb)
            {
                ErrorLog::getErrorLog()->putErrInfo("临时文件里找不到节区 " + replacements[i].name, path);
                return false;
            }
            tb->addr->sh_offset = static_cast<Off>(layouts[i].newOff);
            tb->addr->sh_addr = static_cast<Addr>(layouts[i].newAddr);
            tb->addr->sh_size = static_cast<Xword>(replacements[i].content.size());
        }
        if (relocatePht)
        {
            memcpy(base + phtOff, newPhdrs.data(), newPhdrs.size() * sizeof(Phdr));
            tmpPtrs->elf_hdr->e_phoff = static_cast<Off>(phtOff);
            tmpPtrs->elf_hdr->e_phnum = static_cast<typename Traits::Half>(newPhdrCount);
        }
        else
        {
            memcpy(base + tmpPtrs->elf_hdr->e_phoff, newPhdrs.data(), newPhdrs.size() * sizeof(Phdr));
        }
        if (dynamicFileOffset != 0 || dynamicMoves)
        {
            const uint64_t fixOffset = dynamicMoves ? dynamicFinalOffset : dynamicFileOffset;
            size_t fixCount = dynamicCount;
            if (dynamicMoves)
            {
                for (const auto& rep : replacements)
                    if (rep.name == ".dynamic") fixCount = rep.content.size() / sizeof(Dyn);
            }
            if (!fixDynamic(base, fixOffset, fixCount, dynamicUpdates, newAddrByName, newDynstrSize))
            {
                ErrorLog::getErrorLog()->putErrInfo("更新 .dynamic 条目失败（指定 tag 不存在）", path);
                return false;
            }
        }
        else if (!dynamicUpdates.empty())
        {
            ErrorLog::getErrorLog()->putErrInfo("目标没有 PT_DYNAMIC，无法更新 .dynamic 条目", path);
            return false;
        }
        if (!tmpPtrs->flush())
        {
            ErrorLog::getErrorLog()->putErrInfo("同步临时文件失败", path);
            return false;
        }
    }

    // 7. 验证临时文件，验证通过后再原子替换。
    map<string, pair<uint64_t, uint64_t>> newPosByName;
    for (size_t i = 0; i < replacements.size(); ++i)
        newPosByName[replacements[i].name] = { layouts[i].newOff, layouts[i].newAddr };
    if (!verifyRewritten(tmp.path, path, replacements, startOffset, startPage, newPosByName, newAddrByName))
        return false;
    if (rename(tmp.path.c_str(), path.c_str()) != 0)
    {
        ErrorLog::getErrorLog()->putErrInfo("替换原文件失败", path);
        return false;
    }
    tmp.committed = true;
    (void)ElfUtils::fsyncDirectory(path);
    return true;
}

// 显式实例化：其余翻译单元只通过声明使用这两个实例化，定义集中在此。
template class ElfRewriter<Elf64Traits>;
template class ElfRewriter<Elf32Traits>;
