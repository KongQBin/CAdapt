#pragma once
#include <list>
#include <cstring>
#include <linux/types.h>
#include <memory>
#include "ElfPtrs.h"

// 大部分实现参考/复制至内核源码（include/linux/module.h 中的
// struct modversion_info：crc 宽度随目标字长变化，name 长度为
// 64 - sizeof(crc)）。

// __versions 条目：crc 宽度随 ELF 类别变化（32 位 4 字节，64 位 8 字
// 节；CRC 本身是 32 位值），name 长度为 64 - sizeof(crc)，因此条目
// 在两种类别下都是 64 字节——与内核的 struct modversion_info 一致
// （内核的 MODULE_NAME_LEN 即 64 - sizeof(unsigned long)）。
template <typename Traits>
struct ModVersionInfo;
template <>
struct ModVersionInfo<Elf64Traits>
{
    uint64_t crc;
    char name[64 - sizeof(uint64_t)]; // 56
};
template <>
struct ModVersionInfo<Elf32Traits>
{
    uint32_t crc;
    char name[64 - sizeof(uint32_t)]; // 60
};

template <typename Traits>
class ElfSymInfos
{
public:
    ElfSymInfos(){}
    ElfSymInfos(std::string file, ElfOpenMode mode, bool debug = true);
    ~ElfSymInfos() = default;
    ElfSymInfos(const ElfSymInfos&) = delete;
    ElfSymInfos& operator=(const ElfSymInfos&) = delete;
    void printModInfo();
    void printSymInfo();
    std::string getVermagic();
    std::list<char*>* getMInfo();
    std::map<std::string, unsigned long> getSymVerInfo();
    std::map<std::string, ModVersionInfo<Traits>*> *getVInfo();
    bool isValid() const { return m_valid; }
    bool flush() { return m_valid && m_eptrs && m_eptrs->flush(); }
    // 目标模块的 e_machine；无效时返回 0。
    unsigned short machine() const
    {
        return (m_valid && m_eptrs && m_eptrs->elf_hdr) ? m_eptrs->elf_hdr->e_machine : 0;
    }
    std::string getFilePath() const
    {
        return m_eptrs ? m_eptrs->getFilePath() : "";
    }
private:
    bool initModInfo();
    bool initSymInfo();
    bool m_debug = false;
    bool m_valid = false;
    std::unique_ptr<ElfPtrs<Traits>> m_eptrs;
    std::list<char*> m_minfo;
    std::map<std::string, ModVersionInfo<Traits>*> m_vinfo;
};
