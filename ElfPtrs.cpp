#include "ElfPtrs.h"
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
ElfPtrs::ElfPtrs(int rdwr)
{
    rw = rdwr;
}

ElfPtrs::~ElfPtrs()
{
    if(elf_hdr != MAP_FAILED && elf_hdr != NULL)
    {
        if(rw)
            if(msync((void*)elf_hdr, st.st_size, MS_SYNC) < 0)
                ErrorLog::getErrorLog()->putErrInfo("msync error",filePath);
        munmap((void*)elf_hdr,st.st_size);
        elf_hdr = NULL;
    }
}

int ElfPtrs::initPtrs(const char *path)
{
    if(elf_hdr) return 0;
    strcpy(filePath,path);
    int fd = -1;
    if(!rw)
        fd = open(path, O_RDONLY);
    else
        fd = open(path, O_RDWR);

    if (0 > fd)
    {
        ErrorLog::getErrorLog()->putErrInfo("打开文件失败",filePath);
        return 0;
    }

    // 获取文件状态信息，主要是获取文件大小
    if (0 > fstat(fd, &st))
    {
        ErrorLog::getErrorLog()->putErrInfo("获取文件状态失败",filePath);
        close(fd);
        return 0;
    }
    // 进行内存映射文件，便于后面处理的时候直接操作
    if(!rw)
        elf_hdr = (Elf64_Ehdr *)mmap(0, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    else
        elf_hdr = (Elf64_Ehdr *)mmap(0, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (elf_hdr == MAP_FAILED)
    {
        ErrorLog::getErrorLog()->putErrInfo("内存映射文件失败",filePath);
        close(fd);
        return 0;
    }
    close(fd);

    sh = (Elf64_Shdr *)((char *)elf_hdr + elf_hdr->e_shoff);
    sh_str = sh + elf_hdr->e_shstrndx;
    strtab = (char *)elf_hdr + sh_str->sh_offset;
    // 遍历 节头 每一个 表
    for (int i = 1; i < elf_hdr->e_shnum; i++)
    {
        // 获取指针
        Elf64_Shdr *tables = sh + i;
        // 获取当前节名称
        char *name = strtab + tables->sh_name;
        // 判断是否是 .dynsym 表
        // .dynsym表包含有关动态链接所需的所有符号的信息
        //  该表中的某个位置隐藏了依赖于该新glibc的函数的名称
        if (tables->sh_type == SHT_DYNSYM && 0 == strcmp(name, ".dynsym")) {
          sh_dynsym = tables;
          dynsym = (typeof(dynsym))((char *)elf_hdr + sh_dynsym->sh_offset);
//          printf("找到 .dynsym section\n");
        }
        // 判断是否是 .dynstr 表
        // .dynstr 表包含实际版本的实际字符串（.gnu.version_r 仅是记录值）
        else if (tables->sh_type == SHT_STRTAB && !strcmp(name, ".dynstr"))
        {
          sh_dynstr = tables;
          dynstr = (typeof(dynstr))((char *)elf_hdr + sh_dynstr->sh_offset);
//          printf("找到 .dynstr section\n");
        }
        // 判断是否是 .gnu.version 表
        // .gnu.version表它包含所有动态符号的版本信息
        // .dynsym中列出的每个符号都会在此处有一个对应的条目
        else if (tables->sh_type == SHT_GNU_versym && !strcmp(name, ".gnu.version")) {
          sh_version = tables;
          versions = (typeof(versions))((char *)elf_hdr + sh_version->sh_offset);
//          printf("找到 .gnu.version section\n");
        }
        // 判断是否是 .gnu.version_d 表
        // .gnu.version_d 表包含二进制文件所定义的库版本（因此带有_d后缀）
        // 每个条目都显示版本名称（GLIBC_2.2.5，GLIBC_2.14等），并在其末尾显示“版本号”
        //“版本”号用词不准确，实际上是其他表可以用来引用它的索引。
        else if (tables->sh_type == SHT_GNU_verdef && !strcmp(name, ".gnu.version_d"))
        {
            sh_version_d = tables;
            verdef = (typeof(verdef))((char *)elf_hdr + sh_version_d->sh_offset);
//            printf("找到 .gnu.version_d section\n");
        }
        // 判断是否是 .gnu.version_r 表
        // .gnu.version_r 表包含二进制文件所需的库版本（因此带有_r后缀）
        // 每个条目都显示版本名称（GLIBC_2.2.5，GLIBC_2.14等），并在其末尾显示“版本号”
        //“版本”号用词不准确，实际上是其他表可以用来引用它的索引。
        else if (tables->sh_type == SHT_GNU_verneed && !strcmp(name, ".gnu.version_r")) {
          sh_version_r = tables;
          verneed = (typeof(verneed))((char *)elf_hdr + sh_version_r->sh_offset);
//          printf("找到 .gnu.version_r section\n");
        }
    }
    //sh_version_d版本定义只会在libc库中存在，为了通用性，故注释
    if (!sh_dynsym || !sh_dynstr || !sh_version || !sh_version_r/* || !sh_version_d*/)
    {
        if(!rw && !sh_version_d)
            ErrorLog::getErrorLog()->putErrInfo("没有找到GLIBC有效表节",filePath);
        else
            ErrorLog::getErrorLog()->putErrInfo("没有找到目标ELF有效表节",filePath);
        munmap((void*)elf_hdr,st.st_size);
        elf_hdr = NULL;
        return 0;
    }
    return 1;
}

char *ElfPtrs::getFilePath()
{
    return filePath;
}
