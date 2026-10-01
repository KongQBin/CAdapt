#pragma once
#include <elf.h>
#include <cstddef>
#include <sys/stat.h>
#include <linux/limits.h>
#include <string>
#include <map>
#include "ErrorLog.h"

// 统一的节表信息存储结构
struct TableInfo
{
    bool inited;
    int type;
    std::string name;
    Elf64_Shdr *addr;
    unsigned long str;
    char *strtab;
};

// 读写模式枚举
enum class ElfOpenMode {
    ReadOnly,
    ReadWrite
};

class ElfPtrs
{
public:
    explicit ElfPtrs(ElfOpenMode mode, bool debug = false);
    ~ElfPtrs();
    int initPtrs(const char *path);
    const std::string getFilePath() const;

    // 获取对应的 TableInfo
    TableInfo* getTableInfo(const std::string& name);

    Elf64_Ehdr *elf_hdr         = nullptr;  // 文件头
    Elf64_Shdr *sh              = nullptr;  // 节头表
    Elf64_Shdr *sh_str          = nullptr;  // 节头字符串表
    char *strtab                = nullptr;

private:
    ElfOpenMode m_mode = ElfOpenMode::ReadOnly;
    std::string m_filePath = "";
    struct stat m_st = { 0 };
    bool m_debug = false;

    // 全面采用 map 统一存储所有的 TableInfo
    std::map<std::string, TableInfo> m_tbinfo;
};
