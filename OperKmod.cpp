#include "OperKmod.h"
#include <vector>
ElfModify::ElfModify(string mod) :
    m_einfo(mod,ElfOpenMode::ReadWrite)
{

}

ElfModify::ElfModify(string symver, string mod) :
    m_kinfo(symver), m_einfo(mod,ElfOpenMode::ReadWrite)
{

}

ElfModify::~ElfModify()
{

}

bool ElfModify::motify(string vermagic)
{
    if (!m_einfo.isValid() || !m_kinfo.isValid()) return false;
    bool ret = true;
    // 先记录所有修改；检查完成前不写入共享映射。
    vector<pair<modversion_info*, unsigned long>> patches;
    map<string, modversion_info *> *vinfo = m_einfo.getVInfo();
    for(auto it = vinfo->begin(); it != vinfo->end(); ++it)
    {
        unsigned long crc = 0;
        // 如果 Module.symvers 中没找到，尝试在当前系统正在运行的模块中查找作为兜底
        const bool found = m_kinfo.findSym(it->first, crc) || m_minfo.findSym(it->first, crc);
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
    const string curSysVer = vermagic.empty() ? m_minfo.getVermagic() : vermagic;
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
        printf("modify symbol %-32s crc %lu to %lu\n", patch.first->name, patch.first->crc, patch.second);
        patch.first->crc = patch.second;
    }
    const size_t capacity = strlen(vIndex);
    memset(vIndex, 0, capacity + 1);
    memcpy(vIndex, curSysVer.data(), curSysVer.size());
    printf("modify ver str magic to %s\n", vIndex);
    return m_einfo.flush();
}
