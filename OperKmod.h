#pragma once
#include "./kmod/KSymInfos.h"
#include "./kmod/ElfSymInfos.h"
#include "./kmod/ExistingModInfo.h"
#include <memory>
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
    ElfSymInfos m_einfo;
    // 当前系统中已存在的内核模块中的信息
    std::unique_ptr<ExistingModInfo> m_minfo;
};
