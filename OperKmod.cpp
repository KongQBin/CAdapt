#include "OperKmod.h"
#include "ElfUtils.h"
#include <iostream>
#include <vector>

template <typename Traits>
ElfModify<Traits>::ElfModify(string mod) :
    m_einfo(mod,ElfOpenMode::ReadWrite)
{

}

template <typename Traits>
ElfModify<Traits>::ElfModify(string symver, string mod) :
    m_kinfo(symver), m_einfo(mod,ElfOpenMode::ReadWrite)
{

}

template <typename Traits>
ElfModify<Traits>::~ElfModify()
{

}

template <typename Traits>
ExistingModInfo& ElfModify<Traits>::systemInfo()
{
    if (!m_minfo) m_minfo.reset(new ExistingModInfo());
    return *m_minfo;
}

template <typename Traits>
bool ElfModify<Traits>::motify(string vermagic)
{
    if (!m_einfo.isValid() || !m_kinfo.isValid()) return false;

    // 系统来源的符号 CRC 或 vermagic 只对本机内核的模块有效：目标模块
    // 的架构与当前系统不一致时，系统值不能用，警告并拒绝修改（文件不
    // 动）。显式同时提供 -s 与 -v 时不走系统来源，不做此检查。
    const bool usesSystemCrc = !m_kinfo.hasSource();
    const bool usesSystemVermagic = vermagic.empty();
    if (usesSystemCrc || usesSystemVermagic)
    {
        const unsigned short host = systemInfo().hostMachine();
        const unsigned short target = m_einfo.machine();
        if (host != 0 && target != 0 && host != target)
        {
            const std::string message = "警告：目标模块架构（" + ElfUtils::machineName(target) +
                "）与当前系统（" + ElfUtils::machineName(host) +
                "）不一致，系统提供的符号 CRC/vermagic 可能不适用，拒绝修改";
            std::cerr << message << std::endl;
            ErrorLog::getErrorLog()->putErrInfo(message, m_einfo.getFilePath());
            return false;
        }
    }

    bool ret = true;
    // 先记录所有修改；检查完成前不写入共享映射。
    vector<pair<ModVersionInfo<Traits>*, unsigned long>> patches;
    map<string, ModVersionInfo<Traits> *> *vinfo = m_einfo.getVInfo();
    for(auto it = vinfo->begin(); it != vinfo->end(); ++it)
    {
        unsigned long crc = 0;
        // 显式指定文件时，仅使用该文件，避免混入另一个内核的CRC。
        const bool found = m_kinfo.findSym(it->first, crc) ||
            (!m_kinfo.hasSource() && systemInfo().findSym(it->first, crc));
        if(!found)
        {
            printf("Symbol: %-32s is not found\n",it->first.c_str());
            ErrorLog::getErrorLog()->putErrInfo("目标内核中未找到符号:" + it->first);
            ret = false;
        }
        else
        {
            if(crc != it->second->crc)
            {
                patches.push_back(make_pair(it->second, crc));
            }
        }
    }
    // 校验目标版本魔术及原字段容量。
    const string curSysVer = vermagic.empty() ? systemInfo().getVermagic() : vermagic;
    if (!m_kinfo.hasSource() && !vinfo->empty() && !vermagic.empty() &&
        vermagic != systemInfo().getVermagic())
    {
        ErrorLog::getErrorLog()->putErrInfo("显式vermagic与系统CRC来源不一致，请提供对应的Module.symvers");
        fprintf(stderr, "Target vermagic differs from system CRC source; specify --symvers.\n");
        ret = false;
    }
    char* vIndex = nullptr;
    list<char *>* modinfo = m_einfo.getMInfo();
    for(list<char *>::iterator it = modinfo->begin(); it != modinfo->end(); ++it)
    {
        if(strncmp(*it, "vermagic=", strlen("vermagic=")) != 0) continue;
        vIndex = *it + strlen("vermagic=");
        break;
    }
    if (curSysVer.empty())
    {
        ErrorLog::getErrorLog()->putErrInfo("未能获取目标vermagic，请通过 --vermagic 指定非空版本字符串");
        fprintf(stderr, "Error: target vermagic is empty; specify --vermagic.\n");
        ret = false;
    }
    if (!vIndex || curSysVer.length() > strlen(vIndex))
    {
        ErrorLog::getErrorLog()->putErrInfo("目标vermagic字段不存在或空间不足");
        fprintf(stderr, "Error: target vermagic field is missing or too short.\n");
        ret = false;
    }
    if (!ret) return false;

    for (const auto& patch : patches)
    {
        printf("modify symbol %-32s crc %lu to %lu\n", patch.first->name, (unsigned long)patch.first->crc, patch.second);
        patch.first->crc = patch.second;
    }
    const size_t capacity = strlen(vIndex);
    memset(vIndex, 0, capacity + 1);
    memcpy(vIndex, curSysVer.data(), curSysVer.size());
    printf("modify ver str magic to %s\n", vIndex);
    return m_einfo.flush();
}

// 显式实例化：main.cpp 只通过声明使用这两个实例化，定义集中在此。
template class ElfModify<Elf64Traits>;
template class ElfModify<Elf32Traits>;
