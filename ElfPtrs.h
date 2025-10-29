#pragma once
#include <elf.h>
#include <sys/stat.h>
#include <cstddef> // for size_t
#ifndef __APPLE__
#include <linux/limits.h>
#else
#ifndef PATH_MAX
    #define PATH_MAX 4096
#endif
#endif
#include "ErrorLog.h"

// 使用 C++11 enum class
enum class ElfOpenMode {
    ReadOnly,
    ReadWrite
};

class ElfPtrs
{
public:
    // C++11 explicit
    explicit ElfPtrs(ElfOpenMode mode); 
    ~ElfPtrs();
    int initPtrs(const char *path);
    const char *getFilePath() const; 

    // C++11 成员初始化
    Elf64_Ehdr *elf_hdr = nullptr;
    Elf64_Shdr *sh = nullptr;
    Elf64_Shdr *sh_str = nullptr;
    char *strtab  = nullptr;
    Elf64_Shdr *sh_dynsym = nullptr;
    Elf64_Sym *dynsym = nullptr;
    Elf64_Shdr *sh_dynstr = nullptr;
    char *dynstr = nullptr;
    Elf64_Shdr *sh_version = nullptr;
    unsigned short *versions = nullptr;
    Elf64_Shdr *sh_version_r = nullptr;
    Elf64_Verneed *verneed = nullptr;
    Elf64_Shdr *sh_version_d = nullptr;
    Elf64_Verdef *verdef = nullptr;
private:
    char filePath[PATH_MAX] = { 0 };
    struct stat st;
    ElfOpenMode mode;
};
