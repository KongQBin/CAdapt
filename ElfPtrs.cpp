#include "ElfPtrs.h"
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

ElfPtrs::ElfPtrs(ElfOpenMode mode, bool debug)
    : m_mode(mode), m_debug(debug)
{

}

ElfPtrs::~ElfPtrs()
{
    if(elf_hdr != MAP_FAILED && elf_hdr != nullptr)
    {
        if(m_mode == ElfOpenMode::ReadWrite)
        {
            if(msync(reinterpret_cast<void*>(elf_hdr), m_st.st_size, MS_SYNC) < 0)
                ErrorLog::getErrorLog()->putErrInfo("msync error", m_filePath);
        }
        munmap(reinterpret_cast<void*>(elf_hdr), m_st.st_size);
        elf_hdr = nullptr;
    }
}

int ElfPtrs::initPtrs(const std::string &path)
{
    if(elf_hdr) return 0;
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

    elf_hdr = reinterpret_cast<Elf64_Ehdr *>(mmap(0, m_st.st_size, mmapProt, mmapFlags, fd, 0));

    if (elf_hdr == MAP_FAILED)
    {
        ErrorLog::getErrorLog()->putErrInfo("内存映射文件失败", m_filePath);
        close(fd);
        return 0;
    }
    close(fd);

    sh = reinterpret_cast<Elf64_Shdr *>((char *)elf_hdr + elf_hdr->e_shoff);
    sh_str = sh + elf_hdr->e_shstrndx;
    strtab = reinterpret_cast<char *>(elf_hdr) + sh_str->sh_offset;

    if(m_debug)
    {
        printf("TableInfo:\n");
        printf("\tstrtab addr = %p  %s\n", strtab, strtab);
    }

    // 遍历节头表，统一采用 TableInfo 形式进行存储
    for (int i = 1; i < elf_hdr->e_shnum; i++)
    {
        TableInfo info;
        info.addr = sh + i;
        info.name = strtab + info.addr->sh_name;
        info.type = info.addr->sh_type;
        info.str = info.addr->sh_link;
        info.strtab = reinterpret_cast<char *>(elf_hdr) + sh[info.str].sh_offset;
        info.strtab = (info.strtab == reinterpret_cast<char *>(elf_hdr) ? nullptr : info.strtab);
        info.inited = true;

        if(m_debug)
        {
            printf("\tname: %-32s type: %d addr: %p str: %02lu strtab: %p\n",
                   info.name.c_str(),
                   info.type,
                   info.addr,
                   info.str,
                   info.strtab);
        }

        m_tbinfo[info.name] = info;
    }

    // 复用原有的检查逻辑，现在通过 getTableInfo 动态判断必需节是否存在
    if (!getTableInfo(".dynsym") || !getTableInfo(".dynstr") ||
        !getTableInfo(".gnu.version") || !getTableInfo(".gnu.version_r"))
    {
        ErrorLog::getErrorLog()->putErrInfo("没有找到ELF必需的表节 (.dynsym, .dynstr, .gnu.version, .gnu.version_r)", m_filePath);
        munmap(reinterpret_cast<void*>(elf_hdr), m_st.st_size);
        elf_hdr = nullptr;
        m_tbinfo.clear();
        return 0;
    }

    if (m_mode == ElfOpenMode::ReadOnly && !getTableInfo(".gnu.version_d"))
    {
        ErrorLog::getErrorLog()->putErrInfo("没有找到GLIBC库的 .gnu.version_d 表节", m_filePath);
        munmap(reinterpret_cast<void*>(elf_hdr), m_st.st_size);
        elf_hdr = nullptr;
        m_tbinfo.clear();
        return 0;
    }

    return 1;
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
