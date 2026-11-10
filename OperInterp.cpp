#include "OperInterp.h"
#include "ElfUtils.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>

using namespace std;

namespace {

// 没有与目标匹配的解释器时的统一警告（终端 + 错误日志）。
void warnNoMatchingInterpreter(const std::string& targetPath, const ElfIdentity& targetId,
                               const std::vector<std::string>& candidates)
{
    std::string message = "警告：没有与目标匹配的解释器：目标为 " +
        ElfUtils::describeIdentity(targetId) + "，提供的解释器为 ";
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (i) message += "、";
        ElfIdentity candidateId;
        if (ElfUtils::identifyElf(candidates[i], candidateId))
            message += ElfUtils::describeIdentity(candidateId);
        else
            message += "无法识别(" + candidates[i] + ")";
    }
    message += "，跳过：" + targetPath;
    std::cerr << message << std::endl;
    ErrorLog::getErrorLog()->putErrInfo(message, targetPath);
}

} // namespace

template <typename Traits>
bool OperInterp::validateNewInterpreter(const string& newInterp, typename Traits::Half targetMachine) const
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

    ElfPtrs<Traits> loader(ElfOpenMode::ReadOnly);
    if (!loader.initPtrs(newInterp))
    {
        ErrorLog::getErrorLog()->putErrInfo(
            Traits::elfClass == ELFCLASS64 ? "新解释器不是有效的64位小端ELF"
                                            : "新解释器不是有效的32位小端ELF",
            newInterp);
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

template <typename Traits>
int OperInterp::hasInterpreter(const string& path) const
{
    ElfPtrs<Traits> ptrs(ElfOpenMode::ReadOnly);
    if (!ptrs.initPtrs(path)) return -1;
    return ptrs.findProgramHeader(PT_INTERP) != nullptr ? 1 : 0;
}

template <typename Traits>
bool OperInterp::showInterpreterForFile(const string& path) const
{
    ElfPtrs<Traits> ptrs(ElfOpenMode::ReadOnly);
    if (!ptrs.initPtrs(path))
    {
        ErrorLog::getErrorLog()->putErrInfo("读取目标失败", path);
        return false;
    }
    typename Traits::Phdr* interp = ptrs.findProgramHeader(PT_INTERP);
    if (!interp)
    {
        ErrorLog::getErrorLog()->putErrInfo("目标没有 PT_INTERP", path);
        return false;
    }
    const char* base = reinterpret_cast<const char*>(ptrs.elf_hdr);
    cout << path << ": " << (base + interp->p_offset) << endl;
    return true;
}

template <typename Traits>
bool OperInterp::verifyInterpreter(const std::string& path, const std::string& expected) const
{
    ElfPtrs<Traits> verify(ElfOpenMode::ReadOnly);
    if (!verify.initPtrs(path))
    {
        ErrorLog::getErrorLog()->putErrInfo("写入后重读验证失败", path);
        return false;
    }
    typename Traits::Phdr* verified = verify.findProgramHeader(PT_INTERP);
    if (!verified || std::string(reinterpret_cast<const char*>(verify.elf_hdr) + verified->p_offset) != expected)
    {
        ErrorLog::getErrorLog()->putErrInfo("写入后验证：解释器与预期不符", path);
        return false;
    }
    // LOAD 段的 p_offset 与 p_vaddr 对 p_align 取模必须相等，否则加载器可能拒绝加载。
    for (unsigned i = 0; i < verify.elf_hdr->e_phnum; ++i)
    {
        const typename Traits::Phdr& seg = verify.ph[i];
        if (seg.p_type == PT_LOAD && seg.p_align > 1 &&
            seg.p_offset % seg.p_align != seg.p_vaddr % seg.p_align)
        {
            ErrorLog::getErrorLog()->putErrInfo("写入后验证：LOAD 段偏移与虚址不对齐", path);
            return false;
        }
    }
    return true;
}

template <typename Traits>
bool OperInterp::rewriteWithLongerInterpreter(const string& path, const string& newInterp, const string& oldInterp)
{
    // 重写是 rename 替换，会换 inode：符号链接目标会被替换成本体，必须拒绝。
    struct stat st;
    if (lstat(path.c_str(), &st) != 0)
    {
        ErrorLog::getErrorLog()->putErrInfo("获取目标状态失败", path);
        return false;
    }
    if (S_ISLNK(st.st_mode))
    {
        ErrorLog::getErrorLog()->putErrInfo("更长解释器需要重写文件，不支持符号链接目标", path);
        return false;
    }
    if (!S_ISREG(st.st_mode))
    {
        ErrorLog::getErrorLog()->putErrInfo("目标不是普通文件", path);
        return false;
    }
    const typename Traits::Xword oldSize = static_cast<typename Traits::Xword>(st.st_size);
    const size_t newLen = newInterp.size();
    // 新串追加到文件末尾：其余内容与全部偏移保持不动，PT_INTERP 与
    // .interp 节区改指向新位置。内核直接按 PT_INTERP 的文件偏移读
    // 解释器路径，不要求它落在某个 LOAD 段内。
    const typename Traits::Xword newSize = oldSize + static_cast<typename Traits::Xword>(newLen + 1);

    // 复制到同目录临时文件并扩容；rename 之前任何失败都删临时文件，原文件不动。
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

    {
        unique_ptr<ElfPtrs<Traits>> tmpPtrs(new ElfPtrs<Traits>(ElfOpenMode::ReadWrite));
        if (!tmpPtrs->initPtrs(tmp.path))
        {
            ErrorLog::getErrorLog()->putErrInfo("初始化临时文件失败", path);
            return false;
        }
        char* base = reinterpret_cast<char*>(tmpPtrs->elf_hdr);
        typename Traits::Phdr* interp = tmpPtrs->findProgramHeader(PT_INTERP);
        if (!interp)
        {
            ErrorLog::getErrorLog()->putErrInfo("目标没有 PT_INTERP", path);
            return false;
        }
        // 写新串到文件末尾，并改指 PT_INTERP。
        memcpy(base + oldSize, newInterp.c_str(), newLen + 1);
        interp->p_offset = oldSize;
        interp->p_filesz = static_cast<typename Traits::Xword>(newLen + 1);
        // .interp 节区改指新位置（sh_addr 仍指向旧位置，只改偏移与大小，以 PT_INTERP 为准）。
        TableInfo<Traits>* tb_interp = tmpPtrs->getTableInfo(".interp");
        if (tb_interp)
        {
            tb_interp->addr->sh_offset = oldSize;
            tb_interp->addr->sh_size = static_cast<typename Traits::Xword>(newLen + 1);
        }
        if (!tmpPtrs->flush())
        {
            ErrorLog::getErrorLog()->putErrInfo("同步临时文件失败", path);
            return false;
        }
    }
    if (!verifyInterpreter<Traits>(tmp.path, newInterp))
    {
        ErrorLog::getErrorLog()->putErrInfo("重写后验证临时文件失败", path);
        return false;
    }
    {
        // .interp 节区内容也应是新串（若节区存在）。
        ElfPtrs<Traits> check(ElfOpenMode::ReadOnly);
        if (!check.initPtrs(tmp.path))
        {
            ErrorLog::getErrorLog()->putErrInfo("重写后重读临时文件失败", path);
            return false;
        }
        TableInfo<Traits>* tb_interp = check.getTableInfo(".interp");
        if (tb_interp)
        {
            const char* sectionStr = reinterpret_cast<const char*>(check.elf_hdr) + tb_interp->addr->sh_offset;
            if (string(sectionStr) != newInterp)
            {
                ErrorLog::getErrorLog()->putErrInfo("重写后验证：.interp 节区与预期不符", path);
                return false;
            }
        }
    }

    if (rename(tmp.path.c_str(), path.c_str()) != 0)
    {
        ErrorLog::getErrorLog()->putErrInfo("替换原文件失败", path);
        return false;
    }
    tmp.committed = true;
    (void)ElfUtils::fsyncDirectory(path);

    if (!verifyInterpreter<Traits>(path, newInterp))
    {
        ErrorLog::getErrorLog()->putErrInfo("替换后验证失败", path);
        return false;
    }
    printf("  修改解释器（重写文件，增长 %llu 字节）: %-27s ----> %s  文件:%s\n",
           static_cast<unsigned long long>(newLen + 1), oldInterp.c_str(), newInterp.c_str(), path.c_str());
    return true;
}

template <typename Traits>
bool OperInterp::setInterpreterForFile(const string& path, const string& newInterp, bool dryRun)
{
    unique_ptr<ElfPtrs<Traits>> targetPtrs(new ElfPtrs<Traits>(ElfOpenMode::ReadWrite));
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
    typename Traits::Phdr* interp = targetPtrs->findProgramHeader(PT_INTERP);
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

    // 长度分流在校验与试运行之后：更长的新路径走文件重写，见下文。
    if (newInterp == oldCopy)
    {
        cout << "  未改动（解释器已是目标值）: " << oldCopy << "  文件:" << path << endl;
        return true;
    }
    // 任何写入之前先校验新解释器；校验失败时目标保持不变。
    if (!validateNewInterpreter<Traits>(newInterp, targetPtrs->elf_hdr->e_machine)) return false;

    if (dryRun)
    {
        cout << "  试运行，将修改 " << oldCopy << " ----> " << newInterp;
        if (newLen + 1 > interp->p_filesz) cout << "（新路径更长，需重写文件）";
        cout << "  文件:" << path << endl;
        return true;
    }

    // 新路径更长时走文件重写；原地改写无法容纳。此时尚未写入任何内容。
    if (newLen + 1 > interp->p_filesz)
    {
        targetPtrs.reset();
        return rewriteWithLongerInterpreter<Traits>(path, newInterp, oldCopy);
    }

    char* interpStr = base + interp->p_offset;
    memcpy(interpStr, newInterp.c_str(), newLen + 1);
    if (newLen + 1 < interp->p_filesz)
        memset(interpStr + newLen + 1, 0, interp->p_filesz - (newLen + 1));

    // .interp 节区若存在且与程序头不在同一文件偏移，也同步改写，保持两者一致。
    TableInfo<Traits>* tb_interp = targetPtrs->getTableInfo(".interp");
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
    return verifyInterpreter<Traits>(path, newInterp);
}

template <typename Traits>
bool OperInterp::selectInterpreter(const std::vector<std::string>& candidates, typename Traits::Half targetMachine, std::string& chosen) const
{
    for (const auto& candidate : candidates)
    {
        ElfIdentity id;
        if (!ElfUtils::identifyElf(candidate, id)) continue;
        if (id.elfClass != Traits::elfClass || id.elfData != ELFDATA2LSB || id.machine != targetMachine) continue;
        if (!validateNewInterpreter<Traits>(candidate, targetMachine)) continue;
        chosen = candidate;
        return true;
    }
    return false;
}

template <typename Traits>
FileAdaptResult OperInterp::adaptOneFileFor(const std::string& path, const std::vector<std::string>& newInterps, bool dryRun)
{
    ElfPtrs<Traits> probe(ElfOpenMode::ReadOnly);
    if (!probe.initPtrs(path))
    {
        ErrorLog::getErrorLog()->putErrInfo("目标不是有效的 ELF 文件", path);
        return FileAdaptResult::Failed;
    }
    if (!probe.findProgramHeader(PT_INTERP))
    {
        cout << "  跳过（无 PT_INTERP）: " << path << endl;
        return FileAdaptResult::Skipped;
    }
    const typename Traits::Half machine = probe.elf_hdr->e_machine;
    std::string chosen;
    if (!selectInterpreter<Traits>(newInterps, machine, chosen))
    {
        ElfIdentity targetId;
        ElfUtils::identifyElf(path, targetId);
        warnNoMatchingInterpreter(path, targetId, newInterps);
        return FileAdaptResult::Skipped;
    }
    return setInterpreterForFile<Traits>(path, chosen, dryRun)
        ? FileAdaptResult::Patched : FileAdaptResult::Failed;
}

template <typename Traits>
FileAdaptResult OperInterp::showOneFileFor(const std::string& path) const
{
    const int has = hasInterpreter<Traits>(path);
    if (has < 0) return FileAdaptResult::Failed;
    if (has == 0)
    {
        cout << "  跳过（无 PT_INTERP）: " << path << endl;
        return FileAdaptResult::Skipped;
    }
    return showInterpreterForFile<Traits>(path) ? FileAdaptResult::Patched : FileAdaptResult::Failed;
}

FileAdaptResult OperInterp::adaptOneFile(const std::string& path, const std::vector<std::string>& newInterps, bool dryRun)
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
    if (id.elfClass == ELFCLASS64) return adaptOneFileFor<Elf64Traits>(path, newInterps, dryRun);
    return adaptOneFileFor<Elf32Traits>(path, newInterps, dryRun);
}

FileAdaptResult OperInterp::showOneFile(const std::string& path) const
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

bool OperInterp::adaptTargets(const string& path, const vector<string>& newInterps, bool dryRun)
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
            const FileAdaptResult result = adaptOneFile(file, newInterps, dryRun);
            if (result == FileAdaptResult::Patched) ++patched;
            else if (result == FileAdaptResult::Skipped) ++skipped;
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
    return adaptOneFile(path, newInterps, dryRun) == FileAdaptResult::Patched;
}

bool OperInterp::showTargets(const string& path)
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
