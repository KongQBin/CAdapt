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
    ElfPtrs(const ElfPtrs&) = delete;
    ElfPtrs& operator=(const ElfPtrs&) = delete;
    ~ElfPtrs();
    int initPtrs(const std::string &path);
    bool flush();
    const std::string getFilePath() const;

    // 获取对应的 TableInfo
    TableInfo* getTableInfo(const std::string& name);

    // 查找第一个指定类型的程序头；不存在返回 nullptr。
    // 仅用于 PT_INTERP / PT_DYNAMIC 这类定位，调用前 initPtrs 已校验程序头表。
    Elf64_Phdr* findProgramHeader(Elf64_Word type);

    Elf64_Ehdr *elf_hdr         = nullptr;  // 文件头
    Elf64_Phdr *ph              = nullptr;  // 程序头表（e_phnum 为 0 时为空）
    Elf64_Shdr *sh              = nullptr;  // 节头表
    Elf64_Shdr *sh_str          = nullptr;  // 节头字符串表
    char *strtab                = nullptr;

private:
    bool validateTables() const;
    bool validateProgramHeaders() const;
    bool validateVersions(const Elf64_Shdr& table) const;
    bool validateVersionIndices() const;
    int reject(const std::string& reason);
    ElfOpenMode m_mode = ElfOpenMode::ReadOnly;
    std::string m_filePath = "";
    struct stat m_st = {};
    bool m_debug = false;
    bool m_flushAttempted = false;

    // 全面采用 map 统一存储所有的 TableInfo
    std::map<std::string, TableInfo> m_tbinfo;
};
