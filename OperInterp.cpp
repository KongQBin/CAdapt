#include "OperInterp.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
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

namespace {

// 同目录临时文件的守卫：提交（rename）前析构会关闭 fd 并删除临时文件。
struct TempFileGuard {
    int fd;
    std::string path;
    bool committed;
    TempFileGuard() : fd(-1), committed(false) {}
    ~TempFileGuard()
    {
        if (fd >= 0) close(fd);
        if (!committed && !path.empty()) unlink(path.c_str());
    }
};

bool copyFileTo(int srcFd, int dstFd)
{
    char buffer[65536];
    while (true)
    {
        ssize_t n = read(srcFd, buffer, sizeof(buffer));
        if (n < 0)
        {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return true;
        ssize_t off = 0;
        while (off < n)
        {
            ssize_t w = write(dstFd, buffer + off, static_cast<size_t>(n - off));
            if (w < 0)
            {
                if (errno == EINTR) continue;
                return false;
            }
            off += w;
        }
    }
}

bool fsyncDirectory(const std::string& filePath)
{
    const size_t slash = filePath.find_last_of('/');
    const std::string dir = slash == std::string::npos ? "." : filePath.substr(0, slash + 1);
    const int dirFd = open(dir.c_str(), O_RDONLY);
    if (dirFd < 0) return false;
    const bool ok = fsync(dirFd) == 0;
    close(dirFd);
    return ok;
}

} // namespace

bool OperInterp::verifyInterpreter(const std::string& path, const std::string& expected) const
{
    ElfPtrs verify(ElfOpenMode::ReadOnly);
    if (!verify.initPtrs(path))
    {
        ErrorLog::getErrorLog()->putErrInfo("写入后重读验证失败", path);
        return false;
    }
    Elf64_Phdr* verified = verify.findProgramHeader(PT_INTERP);
    if (!verified || std::string(reinterpret_cast<const char*>(verify.elf_hdr) + verified->p_offset) != expected)
    {
        ErrorLog::getErrorLog()->putErrInfo("写入后验证：解释器与预期不符", path);
        return false;
    }
    // LOAD 段的 p_offset 与 p_vaddr 对 p_align 取模必须相等，否则加载器可能拒绝加载。
    for (unsigned i = 0; i < verify.elf_hdr->e_phnum; ++i)
    {
        const Elf64_Phdr& seg = verify.ph[i];
        if (seg.p_type == PT_LOAD && seg.p_align > 1 &&
            seg.p_offset % seg.p_align != seg.p_vaddr % seg.p_align)
        {
            ErrorLog::getErrorLog()->putErrInfo("写入后验证：LOAD 段偏移与虚址不对齐", path);
            return false;
        }
    }
    return true;
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

    // 长度分流在校验与试运行之后：更长的新路径走文件重写，见下文。
    if (newInterp == oldCopy)
    {
        cout << "  未改动（解释器已是目标值）: " << oldCopy << "  文件:" << path << endl;
        return true;
    }
    // 任何写入之前先校验新解释器；校验失败时目标保持不变。
    if (!validateNewInterpreter(newInterp, targetPtrs->elf_hdr->e_machine)) return false;

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
        return rewriteWithLongerInterpreter(path, newInterp, oldCopy);
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
    return verifyInterpreter(path, newInterp);
}

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
    const Elf64_Xword oldSize = static_cast<Elf64_Xword>(st.st_size);
    const size_t newLen = newInterp.size();
    // 新串追加到文件末尾：其余内容与全部偏移保持不动，PT_INTERP 与
    // .interp 节区改指向新位置。内核直接按 PT_INTERP 的文件偏移读
    // 解释器路径，不要求它落在某个 LOAD 段内。
    const Elf64_Xword newSize = oldSize + static_cast<Elf64_Xword>(newLen + 1);

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
    if (srcFd < 0 || !copyFileTo(srcFd, tmp.fd))
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
        unique_ptr<ElfPtrs> tmpPtrs(new ElfPtrs(ElfOpenMode::ReadWrite));
        if (!tmpPtrs->initPtrs(tmp.path))
        {
            ErrorLog::getErrorLog()->putErrInfo("初始化临时文件失败", path);
            return false;
        }
        char* base = reinterpret_cast<char*>(tmpPtrs->elf_hdr);
        Elf64_Phdr* interp = tmpPtrs->findProgramHeader(PT_INTERP);
        if (!interp)
        {
            ErrorLog::getErrorLog()->putErrInfo("目标没有 PT_INTERP", path);
            return false;
        }
        // 写新串到文件末尾，并改指 PT_INTERP。
        memcpy(base + oldSize, newInterp.c_str(), newLen + 1);
        interp->p_offset = oldSize;
        interp->p_filesz = static_cast<Elf64_Xword>(newLen + 1);
        // .interp 节区改指新位置（sh_addr 仍指向旧位置，只改偏移与大小，以 PT_INTERP 为准）。
        TableInfo* tb_interp = tmpPtrs->getTableInfo(".interp");
        if (tb_interp)
        {
            tb_interp->addr->sh_offset = oldSize;
            tb_interp->addr->sh_size = static_cast<Elf64_Xword>(newLen + 1);
        }
        if (!tmpPtrs->flush())
        {
            ErrorLog::getErrorLog()->putErrInfo("同步临时文件失败", path);
            return false;
        }
    }
    if (!verifyInterpreter(tmp.path, newInterp))
    {
        ErrorLog::getErrorLog()->putErrInfo("重写后验证临时文件失败", path);
        return false;
    }
    {
        // .interp 节区内容也应是新串（若节区存在）。
        ElfPtrs check(ElfOpenMode::ReadOnly);
        if (!check.initPtrs(tmp.path))
        {
            ErrorLog::getErrorLog()->putErrInfo("重写后重读临时文件失败", path);
            return false;
        }
        TableInfo* tb_interp = check.getTableInfo(".interp");
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
    (void)fsyncDirectory(path);

    if (!verifyInterpreter(path, newInterp))
    {
        ErrorLog::getErrorLog()->putErrInfo("替换后验证失败", path);
        return false;
    }
    printf("  修改解释器（重写文件，增长 %llu 字节）: %-27s ----> %s  文件:%s\n",
           static_cast<unsigned long long>(newLen + 1), oldInterp.c_str(), newInterp.c_str(), path.c_str());
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
