#pragma once
#include <list>
#include <cstring>
#include <linux/types.h>
#include "ElfPtrs.h"

// 大部分实现参考/复制至内核源码
// include/linux/moduleparam.h:21
#define MAX_PARAM_PREFIX_LEN (64 - sizeof(unsigned long))
// include/linux/module.h:34
#define MODULE_NAME_LEN MAX_PARAM_PREFIX_LEN
// include/linux/module.h:36
struct modversion_info {
    unsigned long crc;
    char name[MODULE_NAME_LEN];
};

class ElfSymInfos
{
public:
    ElfSymInfos(){}
    ElfSymInfos(std::string file, ElfOpenMode mode, bool debug = true);
    ~ElfSymInfos();
    void printModInfo();
    void printSymInfo();
    std::string getVermagic();
    std::list<char*>* getMInfo();
    std::map<std::string, unsigned long> getSymVerInfo();
    std::map<std::string, modversion_info*> *getVInfo();
private:
    bool initModInfo();
    bool initSymInfo();
    char* nextString(char *str, unsigned long *secsize);
    bool m_debug;
    ElfPtrs *m_eptrs;
    std::string m_vermagic;
    std::list<char*> m_minfo;
    std::map<std::string, modversion_info*> m_vinfo;
};
