#include "ExistingModInfo.h"
#include "ElfUtils.h"
#include <unistd.h>

namespace {

// 按模块自身的 ELF 类别读取 vermagic 与符号 CRC；类别无关的结果
// （字符串与 unsigned long）供上层合并。系统模块与本机内核同架构，
// 理论上都是本机类别；分发一次即可同时覆盖 32 位主机。
template <typename Traits>
bool readSystemModule(const std::string& path, std::string& vermagic,
                      std::map<std::string, unsigned long>& syms)
{
    ElfSymInfos<Traits> infos(path, ElfOpenMode::ReadOnly, false);
    if (!infos.isValid()) return false;
    vermagic = infos.getVermagic();
    syms = infos.getSymVerInfo();
    return true;
}

} // namespace

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
    else
    {
        m_valid = false;
        ErrorLog::getErrorLog()->putErrInfo("uname失败，无法确定系统模块来源");
        return;
    }
    if (!queryKMods()) { m_valid = false; return; }
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
    return m_valid ? m_vermagic : "";
}

bool ExistingModInfo::findSym(const string& symname, unsigned long& crc) const
{
    if (!m_valid) return false;
    auto it = m_va_infos.find(symname);
    if(it == m_va_infos.end()) return false;
    crc = it->second;
    return true;
}

bool ExistingModInfo::queryKMods()
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
        return false;
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
        if (modules.bad()) return false;
        modules.close(); // 关闭文件
    } else {
        ErrorLog::getErrorLog()->putErrInfo("无法读取系统modules.dep", modulespath);
        return false;
    }

    md.sort();
    md.unique(); // 使用自带去重
    for(auto it = md.begin(); it != md.end(); ++it)
    {
        string path = modulespath + *it;
        if(access(path.c_str(), F_OK)) continue;
        list<string> plist = splitString(path,'/');
        m_kmods.insert(make_pair(plist.back(),path));
    }
    return true;
}

void ExistingModInfo::initAllSymbolVerInfo()
{
    for(auto it = m_kmods.begin(); it != m_kmods.end(); ++it)
    {
        std::string newVermagic;
        map<string, unsigned long> tmp;
        ElfIdentity id;
        if (!ElfUtils::identifyElf(it->second, id) || id.elfData == ELFDATA2MSB) continue;
        const bool ok = id.elfClass == ELFCLASS64
            ? readSystemModule<Elf64Traits>(it->second, newVermagic, tmp)
            : readSystemModule<Elf32Traits>(it->second, newVermagic, tmp);
        if (!ok) continue;
        // 获取版本魔术
        if(m_vermagic.empty())
            m_vermagic = newVermagic;
        else if(m_vermagic != newVermagic)
        {
            printf("Warning : old magic = %s new magic = %s\n",
                   m_vermagic.c_str(),newVermagic.c_str());
            m_valid = false;
            ErrorLog::getErrorLog()->putErrInfo("系统模块vermagic冲突", it->second);
        }
        else {}
        // 获取现有驱动的所有符号的CRC信息
        for(auto it2 = tmp.begin(); it2 != tmp.end(); ++it2)
        {
            auto index = m_va_infos.find(it2->first);
            if(index != m_va_infos.end())
            {
                // 发现冲突的CRC校验码
                if(index->second != it2->second)
                {
                    printf("Has repetitive crc name is %-32s %lu and %lu\n", it2->first.c_str(), it2->second, index->second);
                    m_valid = false;
                    ErrorLog::getErrorLog()->putErrInfo("系统模块CRC冲突:" + it2->first, it->second);
                }
            }
            else
                m_va_infos.insert(make_pair(it2->first,it2->second));
        }
    }
    if (!m_valid) { m_va_infos.clear(); m_vermagic.clear(); }
    printf("init %s symbol size = %zu\n",m_va_infos.size() ? "success" : "fail", m_va_infos.size());
//    if(m_va_infos.size())
//    {
//        for(auto it : m_va_infos)
//        {
//            printf("\tname = %-32s crc = %lx\n",it.first.c_str(),it.second);
//        }
//    }
}

unsigned short ExistingModInfo::hostMachine() const
{
    const std::string machine = m_system_info.machine;
    if (machine == "x86_64") return EM_X86_64;
    if (machine == "i386" || machine == "i486" || machine == "i586" || machine == "i686") return EM_386;
    if (machine == "aarch64") return EM_AARCH64;
    if (machine.rfind("arm", 0) == 0) return EM_ARM;
    if (machine == "riscv64" || machine == "riscv32") return EM_RISCV;
    if (machine == "ppc64" || machine == "ppc64le") return EM_PPC64;
    if (machine == "ppc") return EM_PPC;
    if (machine == "s390x") return EM_S390;
    if (machine == "mips" || machine == "mips64") return EM_MIPS;
    return 0;
}

