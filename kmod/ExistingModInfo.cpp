#include "ExistingModInfo.h"
#include <unistd.h>

using namespace std;
ExistingModInfo::ExistingModInfo()
{
    if(!uname(&m_system_info))
    {
        printf("System Name: %s\n", m_system_info.sysname);
        printf("Node Name: %s\n", m_system_info.nodename);
        printf("Release: %s\n", m_system_info.release);
        printf("Version: %s\n", m_system_info.version);
        printf("Machine: %s\n", m_system_info.machine);
#ifdef _GNU_SOURCE
        printf("Domain Name: %s\n", m_system_info.domainname);
#endif
    }
    queryKMods();
    initAllSymbolVerInfo();
}

ExistingModInfo::~ExistingModInfo()
{

}

void ExistingModInfo::printKMods()
{
    for(auto it = m_kmods.begin(); it != m_kmods.end(); ++it)
    {
        printf("%-32s : %s\n",it->first.c_str(),it->second.c_str());
//        cout << it->first << " : " << it->second << endl;
    }
    return;
}

string ExistingModInfo::getVermagic()
{
    return m_vermagic;
}

unsigned long ExistingModInfo::findSym(string symname)
{
    unsigned long ret = 0;
    auto it = m_va_infos.find(symname);
    if(it != m_va_infos.end()) ret = it->second;
    return ret;
}

void ExistingModInfo::queryKMods()
{
    list<string> md;
    string basepath = "/usr/lib/modules/";
    if(access(basepath.c_str(), F_OK))
    {
        cerr << "Path: " << basepath << " is not found!" << endl;
        basepath = "/lib/modules/";
    }
    if(access(basepath.c_str(), F_OK))
    {
        cerr << "Path: " << basepath << " is not found!" << endl;
        return;
    }

    string modulespath = basepath + string(m_system_info.release) + "/";
    ifstream modules(modulespath + "modules.dep");
    if (modules.is_open()) {
        string line;
        while (getline(modules, line)) {
            // 分割出驱动和依赖
            list<string> mod_dep = splitString(line,':');
            if(mod_dep.size() > 1)  // 如果有依赖，那么进一步进行分割
            {
                list<string> tmp;
                tmp.push_back(mod_dep.front());
                for(auto it = ++mod_dep.begin(); it != mod_dep.end(); ++it)
                {
                    list<string> n = splitString(*it,' ');
                    tmp.insert(tmp.end(),n.begin(),n.end());
                }
                mod_dep = tmp;
            }
            md.insert(md.end(),mod_dep.begin(),mod_dep.end());
        }
        modules.close(); // 关闭文件
    } else cerr << "Unable to open file." << endl;

    md.sort();
    md.unique(); // 使用自带去重
    for(auto it = md.begin(); it != md.end(); ++it)
    {
        string path = modulespath + *it;
        if(access(path.c_str(), F_OK)) continue;
        list<string> plist = splitString(path,'/');
        m_kmods.insert(make_pair(plist.back(),path));
    }
}

void ExistingModInfo::initAllSymbolVerInfo()
{
    for(auto it = m_kmods.begin(); it != m_kmods.end(); ++it)
    {
        ElfSymInfos infos(it->second,ElfOpenMode::ReadOnly,false);
        // 获取版本魔术
        string newVermagic = infos.getVermagic();
        if(m_vermagic.empty())
            m_vermagic = newVermagic;
        else if(m_vermagic != newVermagic)
        {
            printf("Warning : old magic = %s new magic = %s\n",
                   m_vermagic.c_str(),newVermagic.c_str());
        }
        else {}
        // 获取现有驱动的所有符号的CRC信息
        map<string, unsigned long> tmp(move(infos.getSymVerInfo()));
        for(auto it2 = tmp.begin(); it2 != tmp.end(); ++it2)
        {
            auto index = m_va_infos.find(it2->first);
            if(index != m_va_infos.end())
            {
                // 发现冲突的CRC校验码
                if(index->second != it2->second)
                {
                    printf("Has repetitive crc name is %-32s %lu and %lu\n", it2->first.c_str(), it2->second, index->second);
                    m_va_infos.insert(make_pair(it2->first,it2->second));
                }
            }
            else
                m_va_infos.insert(make_pair(it2->first,it2->second));
        }
    }
    printf("init %s symbol size = %d\n",m_va_infos.size() ? "success" : "fail", m_va_infos.size());
//    if(m_va_infos.size())
//    {
//        for(auto it : m_va_infos)
//        {
//            printf("\tname = %-32s crc = %lx\n",it.first.c_str(),it.second);
//        }
//    }
}

