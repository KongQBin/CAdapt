#include "ElfPtrs.h"
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <limits>
#include <set>

namespace {
bool inRange(Elf64_Xword offset, Elf64_Xword length, Elf64_Xword size)
{
    return offset <= size && length <= size - offset;
}

bool validString(const char* strings, Elf64_Xword size, Elf64_Xword offset)
{
    return offset < size && memchr(strings + offset, '\0', size - offset);
}
}

int ElfPtrs::reject(const std::string& reason)
{
    ErrorLog::getErrorLog()->putErrInfo(reason, m_filePath);
    if (elf_hdr && elf_hdr != MAP_FAILED) munmap(elf_hdr, m_st.st_size);
    elf_hdr = nullptr;
    sh = nullptr;
    sh_str = nullptr;
    strtab = nullptr;
    m_tbinfo.clear();
    return 0;
}

ElfPtrs::ElfPtrs(ElfOpenMode mode, bool debug)
    : m_mode(mode), m_debug(debug)
{

}

ElfPtrs::~ElfPtrs()
{
    if(elf_hdr != MAP_FAILED && elf_hdr != nullptr)
    {
        if(m_mode == ElfOpenMode::ReadWrite && !m_flushAttempted) flush();
        munmap(reinterpret_cast<void*>(elf_hdr), m_st.st_size);
        elf_hdr = nullptr;
    }
}

bool ElfPtrs::flush()
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

int ElfPtrs::initPtrs(const std::string &path)
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

    if (!S_ISREG(m_st.st_mode) || m_st.st_size < static_cast<off_t>(sizeof(Elf64_Ehdr)) ||
        static_cast<uintmax_t>(m_st.st_size) > std::numeric_limits<size_t>::max())
    {
        close(fd);
        return reject("ELF文件不是普通文件或文件头不完整");
    }

    elf_hdr = reinterpret_cast<Elf64_Ehdr *>(mmap(0, m_st.st_size, mmapProt, mmapFlags, fd, 0));

    if (elf_hdr == MAP_FAILED)
    {
        ErrorLog::getErrorLog()->putErrInfo("内存映射文件失败", m_filePath);
        close(fd);
        elf_hdr = nullptr;
        return 0;
    }
    close(fd);

    const Elf64_Xword fileSize = m_st.st_size;
    if (memcmp(elf_hdr->e_ident, ELFMAG, SELFMAG) != 0 ||
        elf_hdr->e_ident[EI_CLASS] != ELFCLASS64 ||
        elf_hdr->e_ident[EI_DATA] != ELFDATA2LSB ||
        elf_hdr->e_ident[EI_VERSION] != EV_CURRENT || elf_hdr->e_version != EV_CURRENT ||
        elf_hdr->e_ehsize != sizeof(Elf64_Ehdr) ||
        (elf_hdr->e_type != ET_REL && elf_hdr->e_type != ET_EXEC && elf_hdr->e_type != ET_DYN))
        return reject("不支持或无效的ELF文件头（需要64位小端ELF）");

    // 暂不支持扩展节区编号；拒绝而不是使用未校验的索引。
    if (!elf_hdr->e_shnum || elf_hdr->e_shstrndx == SHN_UNDEF ||
        elf_hdr->e_shstrndx >= elf_hdr->e_shnum ||
        elf_hdr->e_shentsize != sizeof(Elf64_Shdr) ||
        elf_hdr->e_shoff < sizeof(Elf64_Ehdr) || elf_hdr->e_shoff % alignof(Elf64_Shdr) ||
        !inRange(elf_hdr->e_shoff, Elf64_Xword(elf_hdr->e_shnum) * sizeof(Elf64_Shdr), fileSize))
        return reject("ELF节头表越界或节区编号不受支持");

    sh = reinterpret_cast<Elf64_Shdr *>((char *)elf_hdr + elf_hdr->e_shoff);
    sh_str = sh + elf_hdr->e_shstrndx;
    for (unsigned i = 0; i < elf_hdr->e_shnum; ++i)
    {
        const Elf64_Shdr& table = sh[i];
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
        TableInfo info;
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

bool ElfPtrs::validateTables() const
{
    const char* base = reinterpret_cast<const char*>(elf_hdr);
    for (unsigned i = 1; i < elf_hdr->e_shnum; ++i)
    {
        const Elf64_Shdr& table = sh[i];
        const std::string name = strtab + table.sh_name;
        if ((name == ".dynsym" && table.sh_type != SHT_DYNSYM) ||
            (name == ".dynstr" && table.sh_type != SHT_STRTAB) ||
            (name == ".gnu.version" && table.sh_type != SHT_GNU_versym) ||
            (name == ".gnu.version_r" && table.sh_type != SHT_GNU_verneed) ||
            (name == ".gnu.version_d" && table.sh_type != SHT_GNU_verdef) ||
            ((name == ".modinfo" || name == "__versions") && table.sh_type != SHT_PROGBITS))
            return false;
        if (name == "__versions" && table.sh_offset % alignof(unsigned long)) return false;
        if (table.sh_type == SHT_DYNSYM || table.sh_type == SHT_SYMTAB)
        {
            if (table.sh_offset % alignof(Elf64_Sym) || table.sh_entsize != sizeof(Elf64_Sym) ||
                table.sh_size % sizeof(Elf64_Sym) || sh[table.sh_link].sh_type != SHT_STRTAB)
                return false;
            const Elf64_Shdr& strings = sh[table.sh_link];
            const Elf64_Sym* symbols = reinterpret_cast<const Elf64_Sym*>(base + table.sh_offset);
            for (size_t j = 0; j < table.sh_size / sizeof(Elf64_Sym); ++j)
                if (!validString(base + strings.sh_offset, strings.sh_size, symbols[j].st_name)) return false;
            if (name == ".dynsym" && std::string(strtab + strings.sh_name) != ".dynstr") return false;
        }
        if (table.sh_type == SHT_GNU_versym)
        {
            const Elf64_Shdr& symbols = sh[table.sh_link];
            if (table.sh_offset % alignof(Elf64_Half) || table.sh_entsize != sizeof(Elf64_Half) ||
                table.sh_size % sizeof(Elf64_Half) || symbols.sh_type != SHT_DYNSYM ||
                symbols.sh_entsize != sizeof(Elf64_Sym) ||
                table.sh_size / sizeof(Elf64_Half) != symbols.sh_size / sizeof(Elf64_Sym)) return false;
            if (name == ".gnu.version" && std::string(strtab + symbols.sh_name) != ".dynsym") return false;
        }
        if ((table.sh_type == SHT_GNU_verdef || table.sh_type == SHT_GNU_verneed) &&
            !validateVersions(table)) return false;
    }
    return validateVersionIndices();
}

bool ElfPtrs::validateVersions(const Elf64_Shdr& table) const
{
    const Elf64_Shdr& strings = sh[table.sh_link];
    if (strings.sh_type != SHT_STRTAB || std::string(strtab + strings.sh_name) != ".dynstr" ||
        table.sh_offset % alignof(Elf64_Word) || !table.sh_info) return false;
    const char* base = reinterpret_cast<const char*>(elf_hdr);
    const char* data = base + table.sh_offset;
    const bool definition = table.sh_type == SHT_GNU_verdef;
    const size_t headerSize = definition ? sizeof(Elf64_Verdef) : sizeof(Elf64_Verneed);
    const size_t auxSize = definition ? sizeof(Elf64_Verdaux) : sizeof(Elf64_Vernaux);
    if (table.sh_info > table.sh_size / headerSize) return false;
    Elf64_Xword offset = 0;
    for (unsigned record = 0; record < table.sh_info; ++record)
    {
        if (!inRange(offset, headerSize, table.sh_size) || offset % alignof(Elf64_Word)) return false;
        unsigned count, aux, next;
        if (definition)
        {
            const Elf64_Verdef* value = reinterpret_cast<const Elf64_Verdef*>(data + offset);
            if (value->vd_version != VER_DEF_CURRENT) return false;
            count = value->vd_cnt; aux = value->vd_aux; next = value->vd_next;
        }
        else
        {
            const Elf64_Verneed* value = reinterpret_cast<const Elf64_Verneed*>(data + offset);
            if (value->vn_version != VER_NEED_CURRENT ||
                !validString(base + strings.sh_offset, strings.sh_size, value->vn_file)) return false;
            count = value->vn_cnt; aux = value->vn_aux; next = value->vn_next;
        }
        if ((record + 1 == table.sh_info) != (next == 0)) return false;
        if (next && (next < headerSize || !inRange(offset, next, table.sh_size))) return false;
        const Elf64_Xword end = next ? offset + next : table.sh_size;
        if (!count || count > (end - offset) / auxSize || aux < headerSize ||
            !inRange(offset, aux, end)) return false;
        Elf64_Xword node = offset + aux;
        for (unsigned j = 0; j < count; ++j)
        {
            if (!inRange(node, auxSize, end) || node % alignof(Elf64_Word)) return false;
            unsigned name, step;
            if (definition)
            {
                const Elf64_Verdaux* value = reinterpret_cast<const Elf64_Verdaux*>(data + node);
                name = value->vda_name; step = value->vda_next;
            }
            else
            {
                const Elf64_Vernaux* value = reinterpret_cast<const Elf64_Vernaux*>(data + node);
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

bool ElfPtrs::validateVersionIndices() const
{
    const char* base = reinterpret_cast<const char*>(elf_hdr);
    std::set<unsigned> known = {0, 1};
    for (unsigned i = 1; i < elf_hdr->e_shnum; ++i)
    {
        const Elf64_Shdr& table = sh[i];
        if (table.sh_type == SHT_GNU_verdef)
        {
            const Elf64_Verdef* value = reinterpret_cast<const Elf64_Verdef*>(base + table.sh_offset);
            for (unsigned j = 0; j < table.sh_info; ++j)
            {
                const unsigned id = value->vd_ndx;
                if (value->vd_flags & VER_FLG_BASE)
                {
                    if (id != 1) return false;
                }
                else if (id < 2 || id >= 0x8000 || !known.insert(id).second) return false;
                value = reinterpret_cast<const Elf64_Verdef*>(reinterpret_cast<const char*>(value) + value->vd_next);
            }
        }
        else if (table.sh_type == SHT_GNU_verneed)
        {
            const Elf64_Verneed* value = reinterpret_cast<const Elf64_Verneed*>(base + table.sh_offset);
            for (unsigned j = 0; j < table.sh_info; ++j)
            {
                const Elf64_Vernaux* aux = reinterpret_cast<const Elf64_Vernaux*>(reinterpret_cast<const char*>(value) + value->vn_aux);
                for (unsigned k = 0; k < value->vn_cnt; ++k)
                {
                    const unsigned id = aux->vna_other & 0x7fff;
                    if (id < 2 || !known.insert(id).second) return false;
                    aux = reinterpret_cast<const Elf64_Vernaux*>(reinterpret_cast<const char*>(aux) + aux->vna_next);
                }
                value = reinterpret_cast<const Elf64_Verneed*>(reinterpret_cast<const char*>(value) + value->vn_next);
            }
        }
    }
    for (unsigned i = 1; i < elf_hdr->e_shnum; ++i)
        if (sh[i].sh_type == SHT_GNU_versym)
        {
            const Elf64_Half* values = reinterpret_cast<const Elf64_Half*>(base + sh[i].sh_offset);
            for (size_t j = 0; j < sh[i].sh_size / sizeof(Elf64_Half); ++j)
                if (!known.count(values[j] & 0x7fff)) return false;
        }
    return true;
}

const std::string ElfPtrs::getFilePath() const
{
    return m_filePath;
}

TableInfo* ElfPtrs::getTableInfo(const std::string& name)
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
