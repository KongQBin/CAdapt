#pragma once
#include <map>
#include <vector>
#include <sstream>
#include <fstream>
#include <iostream>
#include <sys/utsname.h>
#include "ElfSymInfos.h"
#include "Common.h"
typedef struct utsname uname_t;
class ExistingModInfo
{
public:
    ExistingModInfo();
    ~ExistingModInfo();
    void printKMods();
    std::string getVermagic();
    unsigned long findSym(std::string symname);
private:
    // 查询系统中正在使用的内核模块
    void queryKMods();
    // 解析查询到的内核模块的符号信息
    void initAllSymbolVerInfo();
    // 存放的是<内核符号名称,内核符号CRC>
    std::map<std::string, unsigned long> m_va_infos;
    // 存放的是<内核模块名称,内核模块路径>
    std::multimap<std::string, std::string> m_kmods;
    // 利用uname函数获取出的系统信息
    uname_t m_system_info;
    // 存放的是内核版本魔术
    std::string m_vermagic;
};
