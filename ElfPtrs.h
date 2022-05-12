#pragma once
#include <elf.h>
#include <sys/stat.h>
#include <stddef.h>
#ifndef __APPLE__
#include <linux/limits.h>
#else
#ifndef PATH_MAX
    #define PATH_MAX 4096
#endif
#endif
#include "ErrorLog.h"

class ElfPtrs
{
public:
    ElfPtrs(int rdwr);
    ~ElfPtrs();
    int initPtrs(const char *path);
    char *getFilePath(); const

    //ELF文件头，在文件的开始，保存了路线图，描述了该文件的组织情况
    Elf64_Ehdr *elf_hdr = NULL;
    //节头表(Section header table)
    Elf64_Shdr *sh = NULL;
    //节头字符串表
    Elf64_Shdr *sh_str = NULL;
    char *strtab  = NULL;

    // dynsym 表位置
    Elf64_Shdr *sh_dynsym = NULL;
    // 符号信息表
    Elf64_Sym *dynsym = NULL;

    // dynstr 表位置
    Elf64_Shdr *sh_dynstr = NULL;
    // 实际版本的实际字符串
    char *dynstr = NULL;

    // gnu.version 表位置
    Elf64_Shdr *sh_version = NULL;
    // 版本索引值列表
    unsigned short *versions = NULL;

    // gnu.version_r 表位置
    Elf64_Shdr *sh_version_r = NULL;
    // 用于遍历版本依赖节表
    Elf64_Verneed *verneed = NULL;

    // 指向 .gnu.version_d 表位置
    Elf64_Shdr *sh_version_d = NULL;
    // 用于遍历版本define节表
    Elf64_Verdef *verdef = NULL;
private:
    //文件信息
    char filePath[PATH_MAX] = { 0 };
    struct stat st;
    int rw;
};
