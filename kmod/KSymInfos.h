#pragma once
#include <map>
#include <list>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <stdio.h>
#include "Common.h"
using namespace std;
struct KSymInfo
{
    std::string name;
    unsigned long crc;
    std::string origin;
    std::string export_type;
};
class KSymInfos
{
public:
    KSymInfos();
    KSymInfos(std::string path);
    ~KSymInfos();
    void setPath(std::string path);
    void initInfos();
    void printInfos();
    unsigned long findSym(std::string symname);
private:
    std::string m_path;
    std::map<std::string,KSymInfo> m_infos;
};
