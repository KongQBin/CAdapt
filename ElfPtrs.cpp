#include "ElfPtrs.h"
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string> 

ElfPtrs::ElfPtrs(ElfOpenMode mode) : mode(mode)
{
}

ElfPtrs::~ElfPtrs()
{
    if(elf_hdr != MAP_FAILED && elf_hdr != nullptr)
    {
        if(mode == ElfOpenMode::ReadWrite)
        {
            if(msync(reinterpret_cast<void*>(elf_hdr), st.st_size, MS_SYNC) < 0)
                ErrorLog::getErrorLog()->putErrInfo("msync error", filePath);
        }
        munmap(reinterpret_cast<void*>(elf_hdr), st.st_size);
        elf_hdr = nullptr;
    }
}

int ElfPtrs::initPtrs(const char *path)
{
    if(elf_hdr) return 0;
    strncpy(filePath, path, PATH_MAX - 1);
    filePath[PATH_MAX - 1] = '\0'; // 确保安全

    int fd = -1;
    int openFlags = O_RDONLY;
    int mmapProt = PROT_READ;
    int mmapFlags = MAP_SHARED;

    if (mode == ElfOpenMode::ReadWrite)
    {
        openFlags = O_RDWR;
        mmapProt = PROT_READ | PROT_WRITE;
    }

    fd = open(path, openFlags);
    if (0 > fd)
    {
        ErrorLog::getErrorLog()->putErrInfo("打开文件失败", filePath);
        return 0;
    }

    if (0 > fstat(fd, &st))
    {
        ErrorLog::getErrorLog()->putErrInfo("获取文件状态失败", filePath);
        close(fd);
        return 0;
    }

    elf_hdr = reinterpret_cast<Elf64_Ehdr *>(mmap(0, st.st_size, mmapProt, mmapFlags, fd, 0));

    if (elf_hdr == MAP_FAILED)
    {
        ErrorLog::getErrorLog()->putErrInfo("内存映射文件失败", filePath);
        close(fd);
        return 0;
    }
    close(fd); 

    sh = reinterpret_cast<Elf64_Shdr *>((char *)elf_hdr + elf_hdr->e_shoff);
    sh_str = sh + elf_hdr->e_shstrndx;
    strtab = reinterpret_cast<char *>(elf_hdr) + sh_str->sh_offset;

    for (int i = 1; i < elf_hdr->e_shnum; i++)
    {
        Elf64_Shdr *tables = sh + i;
        char *name = strtab + tables->sh_name;

        // C++98/11 reinterpret_cast
        if (tables->sh_type == SHT_DYNSYM && 0 == strcmp(name, ".dynsym")) {
          sh_dynsym = tables;
          dynsym = reinterpret_cast<Elf64_Sym*>((char *)elf_hdr + sh_dynsym->sh_offset);
        }
        else if (tables->sh_type == SHT_STRTAB && !strcmp(name, ".dynstr"))
        {
          sh_dynstr = tables;
          dynstr = reinterpret_cast<char*>((char *)elf_hdr + sh_dynstr->sh_offset);
        }
        else if (tables->sh_type == SHT_GNU_versym && !strcmp(name, ".gnu.version")) {
          sh_version = tables;
          versions = reinterpret_cast<unsigned short*>((char *)elf_hdr + sh_version->sh_offset);
        }
        else if (tables->sh_type == SHT_GNU_verdef && !strcmp(name, ".gnu.version_d"))
        {
            sh_version_d = tables;
            verdef = reinterpret_cast<Elf64_Verdef*>((char *)elf_hdr + sh_version_d->sh_offset);
        }
        else if (tables->sh_type == SHT_GNU_verneed && !strcmp(name, ".gnu.version_r")) {
          sh_version_r = tables;
          verneed = reinterpret_cast<Elf64_Verneed*>((char *)elf_hdr + sh_version_r->sh_offset);
        }
    }

    // 修复节检查逻辑
    if (!sh_dynsym || !sh_dynstr || !sh_version || !sh_version_r)
    {
        ErrorLog::getErrorLog()->putErrInfo("没有找到ELF必需的表节 (.dynsym, .dynstr, .gnu.version, .gnu.version_r)", filePath);
        munmap(reinterpret_cast<void*>(elf_hdr), st.st_size);
        elf_hdr = nullptr;
        return 0;
    }

    if (mode == ElfOpenMode::ReadOnly && !sh_version_d)
    {
        ErrorLog::getErrorLog()->putErrInfo("没有找到GLIBC库的 .gnu.version_d 表节", filePath);
        munmap(reinterpret_cast<void*>(elf_hdr), st.st_size);
        elf_hdr = nullptr;
        return 0;
    }

    return 1;
}

const char *ElfPtrs::getFilePath() const
{
    return filePath;
}
