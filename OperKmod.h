#pragma once
#include "./kmod/KSymInfos.h"
#include "./kmod/ElfSymInfos.h"
#include "./kmod/ExistingModInfo.h"
#include <memory>

// 内核模块（.ko）适配：按目标模块的 ELF 类别模板化
// （Elf32Traits/Elf64Traits），32 位模块的 __versions 条目用 4 字节
// CRC，大端模块由上层识别后警告并拒绝。目标模块的架构与当前系统
// 不一致、而符号 CRC 或 vermagic 又取自当前系统时，警告并拒绝修
// 改（系统值只对本机内核的模块有效）。
template <typename Traits>
class ElfModify
{
public:
    ElfModify(string mod);
    ElfModify(string symver, string mod);
    ~ElfModify();
    bool motify(string vermagic = "");
private:
    ExistingModInfo& systemInfo();
    // Module.symvers文件中的信息
    KSymInfos m_kinfo;
    // 要被修改的内核模块信息
    ElfSymInfos<Traits> m_einfo;
    // 当前系统中已存在的内核模块中的信息
    std::unique_ptr<ExistingModInfo> m_minfo;
};
