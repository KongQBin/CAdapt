#pragma once
#include <elf.h>
#include <cstddef>
#include <sys/stat.h>
#include <linux/limits.h>
#include <string>
#include <map>
#include "ErrorLog.h"

// 按 ELF 类别（32/64 位）描述元数据结构宽度的特征组。ElfPtrs 按特征
// 组模板化，同一份代码同时服务两种类别；字节序仍只支持小端，大端
// 文件由上层识别后警告并跳过（见 ElfUtils::identifyElf）。
//
// 注意 32/64 位结构体的差异不只是宽度：Elf32_Sym 只有 16 字节且字段
// 顺序与 Elf64_Sym 不同（value/size 在 info/other/shndx 之前）；
// Elf32_Dyn 8 字节、Elf64_Dyn 16 字节；Verneed/Vernaux/Verdef/Verdaux
// 两种类别布局相同，但为统一仍经特征组取用。
struct Elf64Traits
{
    using Ehdr = Elf64_Ehdr;
    using Phdr = Elf64_Phdr;
    using Shdr = Elf64_Shdr;
    using Dyn = Elf64_Dyn;
    using Sym = Elf64_Sym;
    using Verneed = Elf64_Verneed;
    using Vernaux = Elf64_Vernaux;
    using Verdef = Elf64_Verdef;
    using Verdaux = Elf64_Verdaux;
    using Half = Elf64_Half;
    using Word = Elf64_Word;
    using Xword = Elf64_Xword;
    using Addr = Elf64_Addr;
    using Off = Elf64_Off;
    enum : unsigned char { elfClass = ELFCLASS64 };
};

struct Elf32Traits
{
    using Ehdr = Elf32_Ehdr;
    using Phdr = Elf32_Phdr;
    using Shdr = Elf32_Shdr;
    using Dyn = Elf32_Dyn;
    using Sym = Elf32_Sym;
    using Verneed = Elf32_Verneed;
    using Vernaux = Elf32_Vernaux;
    using Verdef = Elf32_Verdef;
    using Verdaux = Elf32_Verdaux;
    using Half = Elf32_Half;
    using Word = Elf32_Word;
    using Xword = Elf32_Word; // Elf32 没有 Xword：32 位字段即最宽字段。
    using Addr = Elf32_Addr;
    using Off = Elf32_Off;
    enum : unsigned char { elfClass = ELFCLASS32 };
};

// 统一的节表信息存储结构
template <typename Traits>
struct TableInfo
{
    bool inited;
    int type;
    std::string name;
    typename Traits::Shdr *addr;
    unsigned long str;
    char *strtab;
};

// 读写模式枚举
enum class ElfOpenMode {
    ReadOnly,
    ReadWrite
};

template <typename Traits>
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
    TableInfo<Traits>* getTableInfo(const std::string& name);

    // 查找第一个指定类型的程序头；不存在返回 nullptr。
    // 仅用于 PT_INTERP / PT_DYNAMIC 这类定位，调用前 initPtrs 已校验程序头表。
    typename Traits::Phdr* findProgramHeader(typename Traits::Word type);

    typename Traits::Ehdr *elf_hdr = nullptr; // 文件头
    typename Traits::Phdr *ph      = nullptr; // 程序头表（e_phnum 为 0 时为空）
    typename Traits::Shdr *sh      = nullptr; // 节头表
    typename Traits::Shdr *sh_str  = nullptr; // 节头字符串表
    char *strtab                = nullptr;

private:
    bool validateTables() const;
    bool validateProgramHeaders() const;
    bool validateVersions(const typename Traits::Shdr& table) const;
    bool validateVersionIndices() const;
    int reject(const std::string& reason);
    ElfOpenMode m_mode = ElfOpenMode::ReadOnly;
    std::string m_filePath = "";
    struct stat m_st = {};
    bool m_debug = false;
    bool m_flushAttempted = false;

    // 全面采用 map 统一存储所有的 TableInfo
    std::map<std::string, TableInfo<Traits>> m_tbinfo;
};

// 常用实例化别名。
using Elf64Ptrs = ElfPtrs<Elf64Traits>;
using Elf32Ptrs = ElfPtrs<Elf32Traits>;
