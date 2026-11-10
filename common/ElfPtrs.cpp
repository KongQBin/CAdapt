#include "ElfPtrs.h"
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <limits>
#include <set>

namespace {
// 偏移/长度统一按 64 位比较：32 位文件的字段值（32 位宽）隐式提升后仍正确。
bool inRange(uint64_t offset, uint64_t length, uint64_t size)
{
    return offset <= size && length <= size - offset;
}

bool validString(const char* strings, uint64_t size, uint64_t offset)
{
    return offset < size && memchr(strings + offset, '\0', size - offset);
}
}

template <typename Traits>
int ElfPtrs<Traits>::reject(const std::string& reason)
{
    ErrorLog::getErrorLog()->putErrInfo(reason, m_filePath);
    if (elf_hdr && elf_hdr != MAP_FAILED) munmap(elf_hdr, m_st.st_size);
    elf_hdr = nullptr;
    ph = nullptr;
    sh = nullptr;
    sh_str = nullptr;
    strtab = nullptr;
    m_tbinfo.clear();
    return 0;
}

template <typename Traits>
ElfPtrs<Traits>::ElfPtrs(ElfOpenMode mode, bool debug)
    : m_mode(mode), m_debug(debug)
{

}

template <typename Traits>
ElfPtrs<Traits>::~ElfPtrs()
{
    if(elf_hdr != MAP_FAILED && elf_hdr != nullptr)
    {
        if(m_mode == ElfOpenMode::ReadWrite && !m_flushAttempted) flush();
        munmap(reinterpret_cast<void*>(elf_hdr), m_st.st_size);
        elf_hdr = nullptr;
        ph = nullptr;
    }
}

template <typename Traits>
bool ElfPtrs<Traits>::flush()
{
    if (!elf_hdr || elf_hdr == MAP_FAILED) return false;
    if (m_mode == ElfOpenMode::ReadOnly) return true;
    m_flushAttempted = true;
    if (msync(reinterpret_cast<void*>(elf_hdr), m_st.st_size, MS_SYNC) < 0)
    {
        ErrorLog::getErrorLog()->putErrInfo("msync error", m_filePath);
        return false;
    }
    return true;
}

template <typename Traits>
int ElfPtrs<Traits>::initPtrs(const std::string &path)
{
    if(elf_hdr) return 0;
    m_flushAttempted = false;
    m_filePath = path;

    int fd = -1;
    int openFlags = O_RDONLY;
    int mmapProt = PROT_READ;
    int mmapFlags = MAP_SHARED;

    if (m_mode == ElfOpenMode::ReadWrite)
    {
        openFlags = O_RDWR;
        mmapProt = PROT_READ | PROT_WRITE;
    }

    fd = open(path.c_str(), openFlags);
    if (0 > fd)
    {
        ErrorLog::getErrorLog()->putErrInfo("打开文件失败", m_filePath);
        return 0;
    }

    if (0 > fstat(fd, &m_st))
    {
        ErrorLog::getErrorLog()->putErrInfo("获取文件状态失败", m_filePath);
        close(fd);
        return 0;
    }

    if (!S_ISREG(m_st.st_mode) || m_st.st_size < static_cast<off_t>(sizeof(typename Traits::Ehdr)) ||
        static_cast<uintmax_t>(m_st.st_size) > std::numeric_limits<size_t>::max())
    {
        close(fd);
        return reject("ELF文件不是普通文件或文件头不完整");
    }

    elf_hdr = reinterpret_cast<typename Traits::Ehdr *>(mmap(0, m_st.st_size, mmapProt, mmapFlags, fd, 0));

    if (elf_hdr == MAP_FAILED)
    {
        ErrorLog::getErrorLog()->putErrInfo("内存映射文件失败", m_filePath);
        close(fd);
        elf_hdr = nullptr;
        return 0;
    }
    close(fd);

    const uint64_t fileSize = m_st.st_size;
    if (memcmp(elf_hdr->e_ident, ELFMAG, SELFMAG) != 0 ||
        elf_hdr->e_ident[EI_CLASS] != Traits::elfClass ||
        elf_hdr->e_ident[EI_DATA] != ELFDATA2LSB ||
        elf_hdr->e_ident[EI_VERSION] != EV_CURRENT || elf_hdr->e_version != EV_CURRENT ||
        elf_hdr->e_ehsize != sizeof(typename Traits::Ehdr) ||
        (elf_hdr->e_type != ET_REL && elf_hdr->e_type != ET_EXEC && elf_hdr->e_type != ET_DYN))
        return reject(Traits::elfClass == ELFCLASS64
            ? "不支持或无效的ELF文件头（需要64位小端ELF）"
            : "不支持或无效的ELF文件头（需要32位小端ELF）");

    // 程序头表：e_phnum 为 0（如 ET_REL / .ko）时不要求存在；否则先校验表范围
    // 与内容，再继续校验节头表。旧流程只看节头，带伪造 e_phoff 的文件本可绕过。
    if (elf_hdr->e_phnum == PN_XNUM)
        return reject("ELF程序头数量使用扩展编号，不受支持");
    if (elf_hdr->e_phnum)
    {
        if (elf_hdr->e_phentsize != sizeof(typename Traits::Phdr) ||
            elf_hdr->e_phoff < sizeof(typename Traits::Ehdr) || elf_hdr->e_phoff % alignof(typename Traits::Phdr) ||
            !inRange(elf_hdr->e_phoff, uint64_t(elf_hdr->e_phnum) * sizeof(typename Traits::Phdr), fileSize))
            return reject("ELF程序头表越界或不受支持");
        ph = reinterpret_cast<typename Traits::Phdr *>((char *)elf_hdr + elf_hdr->e_phoff);
        if (!validateProgramHeaders())
            return reject("ELF程序头内容无效");
    }

    // 暂不支持扩展节区编号；拒绝而不是使用未校验的索引。
    if (!elf_hdr->e_shnum || elf_hdr->e_shstrndx == SHN_UNDEF ||
        elf_hdr->e_shstrndx >= elf_hdr->e_shnum ||
        elf_hdr->e_shentsize != sizeof(typename Traits::Shdr) ||
        elf_hdr->e_shoff < sizeof(typename Traits::Ehdr) || elf_hdr->e_shoff % alignof(typename Traits::Shdr) ||
        !inRange(elf_hdr->e_shoff, uint64_t(elf_hdr->e_shnum) * sizeof(typename Traits::Shdr), fileSize))
        return reject("ELF节头表越界或节区编号不受支持");

    sh = reinterpret_cast<typename Traits::Shdr *>((char *)elf_hdr + elf_hdr->e_shoff);
    sh_str = sh + elf_hdr->e_shstrndx;
    for (unsigned i = 0; i < elf_hdr->e_shnum; ++i)
    {
        const typename Traits::Shdr& table = sh[i];
        if (table.sh_link >= elf_hdr->e_shnum ||
            (table.sh_type != SHT_NOBITS && !inRange(table.sh_offset, table.sh_size, fileSize)))
            return reject("ELF节区范围或关联索引越界");
        if (table.sh_type == SHT_STRTAB &&
            (!table.sh_size || reinterpret_cast<char*>(elf_hdr)[table.sh_offset] != '\0' ||
             reinterpret_cast<char*>(elf_hdr)[table.sh_offset + table.sh_size - 1] != '\0'))
            return reject("ELF字符串表缺少终止符");
    }
    if (sh_str->sh_type != SHT_STRTAB) return reject("ELF节名字符串表类型错误");
    strtab = reinterpret_cast<char *>(elf_hdr) + sh_str->sh_offset;

    for (unsigned i = 0; i < elf_hdr->e_shnum; ++i)
        if (!validString(strtab, sh_str->sh_size, sh[i].sh_name))
            return reject("ELF节名偏移越界或缺少终止符");

    if(m_debug)
    {
        printf("TableInfo:\n");
        printf("\tstrtab addr = %p  %s\n", static_cast<void*>(strtab), strtab);
    }

    // 遍历节头表，统一采用 TableInfo 形式进行存储
    for (int i = 1; i < elf_hdr->e_shnum; i++)
    {
        TableInfo<Traits> info;
        info.addr = sh + i;
        info.name = strtab + info.addr->sh_name;
        if ((info.name == ".dynsym" || info.name == ".dynstr" || info.name == ".gnu.version" ||
             info.name == ".gnu.version_r" || info.name == ".gnu.version_d" ||
             info.name == ".modinfo" || info.name == "__versions") && m_tbinfo.count(info.name))
            return reject("ELF业务节区名称重复");
        info.type = info.addr->sh_type;
        info.str = info.addr->sh_link;
        info.strtab = info.str && sh[info.str].sh_type == SHT_STRTAB
            ? reinterpret_cast<char *>(elf_hdr) + sh[info.str].sh_offset : nullptr;
        info.inited = true;

        if(m_debug)
        {
            printf("\tname: %-32s type: %d addr: %p str: %02lu strtab: %p\n",
                   info.name.c_str(),
                   info.type,
                   static_cast<void*>(info.addr),
                   info.str,
                   static_cast<void*>(info.strtab));
        }

        m_tbinfo[info.name] = info;
    }

    if (!validateTables()) return reject("ELF符号表或版本节区内容无效");
    // 各业务流程自行检查所需节区；内核模块没有 GLIBC 版本节区。
    return 1;
}

template <typename Traits>
bool ElfPtrs<Traits>::validateProgramHeaders() const
{
    const char* base = reinterpret_cast<const char*>(elf_hdr);
    const uint64_t fileSize = static_cast<uint64_t>(m_st.st_size);
    unsigned interpCount = 0;
    unsigned dynamicCount = 0;
    for (unsigned i = 0; i < elf_hdr->e_phnum; ++i)
    {
        const typename Traits::Phdr& segment = ph[i];
        if (segment.p_filesz &&
            !inRange(segment.p_offset, segment.p_filesz, fileSize))
            return false;
        if (segment.p_type == PT_INTERP)
        {
            if (++interpCount > 1) return false;
            // 解释器路径必须在文件内以 NUL 结尾；长度至少能放 "/x\0"。
            if (segment.p_filesz < 3 ||
                !memchr(base + segment.p_offset, '\0', segment.p_filesz))
                return false;
        }
        else if (segment.p_type == PT_DYNAMIC)
        {
            if (++dynamicCount > 1) return false;
            if (segment.p_filesz % sizeof(typename Traits::Dyn) != 0) return false;
        }
    }
    return true;
}

template <typename Traits>
typename Traits::Phdr* ElfPtrs<Traits>::findProgramHeader(typename Traits::Word type)
{
    if (!ph) return nullptr;
    for (unsigned i = 0; i < elf_hdr->e_phnum; ++i)
        if (ph[i].p_type == type) return ph + i;
    return nullptr;
}

template <typename Traits>
bool ElfPtrs<Traits>::validateTables() const
{
    const char* base = reinterpret_cast<const char*>(elf_hdr);
    for (unsigned i = 1; i < elf_hdr->e_shnum; ++i)
    {
        const typename Traits::Shdr& table = sh[i];
        const std::string name = strtab + table.sh_name;
        if ((name == ".dynsym" && table.sh_type != SHT_DYNSYM) ||
            (name == ".dynstr" && table.sh_type != SHT_STRTAB) ||
            (name == ".gnu.version" && table.sh_type != SHT_GNU_versym) ||
            (name == ".gnu.version_r" && table.sh_type != SHT_GNU_verneed) ||
            (name == ".gnu.version_d" && table.sh_type != SHT_GNU_verdef) ||
            ((name == ".modinfo" || name == "__versions") && table.sh_type != SHT_PROGBITS))
            return false;
        // __versions 条目按目标类别对齐：32 位 4 字节，64 位 8 字节。
        if (name == "__versions" && table.sh_offset % alignof(typename Traits::Addr)) return false;
        if (table.sh_type == SHT_DYNSYM || table.sh_type == SHT_SYMTAB)
        {
            if (table.sh_offset % alignof(typename Traits::Sym) || table.sh_entsize != sizeof(typename Traits::Sym) ||
                table.sh_size % sizeof(typename Traits::Sym) || sh[table.sh_link].sh_type != SHT_STRTAB)
                return false;
            const typename Traits::Shdr& strings = sh[table.sh_link];
            const typename Traits::Sym* symbols = reinterpret_cast<const typename Traits::Sym*>(base + table.sh_offset);
            for (size_t j = 0; j < table.sh_size / sizeof(typename Traits::Sym); ++j)
                if (!validString(base + strings.sh_offset, strings.sh_size, symbols[j].st_name)) return false;
            if (name == ".dynsym" && std::string(strtab + strings.sh_name) != ".dynstr") return false;
        }
        if (table.sh_type == SHT_GNU_versym)
        {
            const typename Traits::Shdr& symbols = sh[table.sh_link];
            if (table.sh_offset % alignof(typename Traits::Half) || table.sh_entsize != sizeof(typename Traits::Half) ||
                table.sh_size % sizeof(typename Traits::Half) || symbols.sh_type != SHT_DYNSYM ||
                symbols.sh_entsize != sizeof(typename Traits::Sym) ||
                table.sh_size / sizeof(typename Traits::Half) != symbols.sh_size / sizeof(typename Traits::Sym)) return false;
            if (name == ".gnu.version" && std::string(strtab + symbols.sh_name) != ".dynsym") return false;
        }
        if ((table.sh_type == SHT_GNU_verdef || table.sh_type == SHT_GNU_verneed) &&
            !validateVersions(table)) return false;
    }
    return validateVersionIndices();
}

template <typename Traits>
bool ElfPtrs<Traits>::validateVersions(const typename Traits::Shdr& table) const
{
    const typename Traits::Shdr& strings = sh[table.sh_link];
    if (strings.sh_type != SHT_STRTAB || std::string(strtab + strings.sh_name) != ".dynstr" ||
        table.sh_offset % alignof(typename Traits::Word) || !table.sh_info) return false;
    const char* base = reinterpret_cast<const char*>(elf_hdr);
    const char* data = base + table.sh_offset;
    const bool definition = table.sh_type == SHT_GNU_verdef;
    const size_t headerSize = definition ? sizeof(typename Traits::Verdef) : sizeof(typename Traits::Verneed);
    const size_t auxSize = definition ? sizeof(typename Traits::Verdaux) : sizeof(typename Traits::Vernaux);
    if (table.sh_info > table.sh_size / headerSize) return false;
    typename Traits::Xword offset = 0;
    for (unsigned record = 0; record < table.sh_info; ++record)
    {
        if (!inRange(offset, headerSize, table.sh_size) || offset % alignof(typename Traits::Word)) return false;
        unsigned count, aux, next;
        if (definition)
        {
            const typename Traits::Verdef* value = reinterpret_cast<const typename Traits::Verdef*>(data + offset);
            if (value->vd_version != VER_DEF_CURRENT) return false;
            count = value->vd_cnt; aux = value->vd_aux; next = value->vd_next;
        }
        else
        {
            const typename Traits::Verneed* value = reinterpret_cast<const typename Traits::Verneed*>(data + offset);
            if (value->vn_version != VER_NEED_CURRENT ||
                !validString(base + strings.sh_offset, strings.sh_size, value->vn_file)) return false;
            count = value->vn_cnt; aux = value->vn_aux; next = value->vn_next;
        }
        if ((record + 1 == table.sh_info) != (next == 0)) return false;
        if (next && (next < headerSize || !inRange(offset, next, table.sh_size))) return false;
        const typename Traits::Xword end = next ? offset + next : table.sh_size;
        if (!count || count > (end - offset) / auxSize || aux < headerSize ||
            !inRange(offset, aux, end)) return false;
        typename Traits::Xword node = offset + aux;
        for (unsigned j = 0; j < count; ++j)
        {
            if (!inRange(node, auxSize, end) || node % alignof(typename Traits::Word)) return false;
            unsigned name, step;
            if (definition)
            {
                const typename Traits::Verdaux* value = reinterpret_cast<const typename Traits::Verdaux*>(data + node);
                name = value->vda_name; step = value->vda_next;
            }
            else
            {
                const typename Traits::Vernaux* value = reinterpret_cast<const typename Traits::Vernaux*>(data + node);
                name = value->vna_name; step = value->vna_next;
            }
            if (!validString(base + strings.sh_offset, strings.sh_size, name) ||
                ((j + 1 == count) != (step == 0))) return false;
            if (step && (step < auxSize || !inRange(node, step, end))) return false;
            node += step;
        }
        offset += next;
    }
    return true;
}

template <typename Traits>
bool ElfPtrs<Traits>::validateVersionIndices() const
{
    const char* base = reinterpret_cast<const char*>(elf_hdr);
    std::set<unsigned> known = {0, 1};
    for (unsigned i = 1; i < elf_hdr->e_shnum; ++i)
    {
        const typename Traits::Shdr& table = sh[i];
        if (table.sh_type == SHT_GNU_verdef)
        {
            const typename Traits::Verdef* value = reinterpret_cast<const typename Traits::Verdef*>(base + table.sh_offset);
            for (unsigned j = 0; j < table.sh_info; ++j)
            {
                const unsigned id = value->vd_ndx;
                if (value->vd_flags & VER_FLG_BASE)
                {
                    if (id != 1) return false;
                }
                else if (id < 2 || id >= 0x8000 || !known.insert(id).second) return false;
                value = reinterpret_cast<const typename Traits::Verdef*>(reinterpret_cast<const char*>(value) + value->vd_next);
            }
        }
        else if (table.sh_type == SHT_GNU_verneed)
        {
            const typename Traits::Verneed* value = reinterpret_cast<const typename Traits::Verneed*>(base + table.sh_offset);
            for (unsigned j = 0; j < table.sh_info; ++j)
            {
                const typename Traits::Vernaux* aux = reinterpret_cast<const typename Traits::Vernaux*>(reinterpret_cast<const char*>(value) + value->vn_aux);
                for (unsigned k = 0; k < value->vn_cnt; ++k)
                {
                    const unsigned id = aux->vna_other & 0x7fff;
                    if (id < 2 || !known.insert(id).second) return false;
                    aux = reinterpret_cast<const typename Traits::Vernaux*>(reinterpret_cast<const char*>(aux) + aux->vna_next);
                }
                value = reinterpret_cast<const typename Traits::Verneed*>(reinterpret_cast<const char*>(value) + value->vn_next);
            }
        }
    }
    for (unsigned i = 1; i < elf_hdr->e_shnum; ++i)
        if (sh[i].sh_type == SHT_GNU_versym)
        {
            const typename Traits::Half* values = reinterpret_cast<const typename Traits::Half*>(base + sh[i].sh_offset);
            for (size_t j = 0; j < sh[i].sh_size / sizeof(typename Traits::Half); ++j)
                if (!known.count(values[j] & 0x7fff)) return false;
        }
    return true;
}

template <typename Traits>
const std::string ElfPtrs<Traits>::getFilePath() const
{
    return m_filePath;
}

template <typename Traits>
TableInfo<Traits>* ElfPtrs<Traits>::getTableInfo(const std::string& name)
{
    auto it = m_tbinfo.find(name);
    if (it != m_tbinfo.end())
    {
        return &(it->second);
    }

    if (m_debug)
    {
        printf("Table %s is not found!!!\n", name.c_str());
    }
    return nullptr;
}

// 显式实例化：其余翻译单元只通过声明使用这两个实例化，定义集中在此。
template class ElfPtrs<Elf64Traits>;
template class ElfPtrs<Elf32Traits>;
