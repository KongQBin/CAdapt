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
    bool initInfos();
    void printInfos();
    bool findSym(const std::string& symname, unsigned long& crc) const;
    bool isValid() const { return m_valid; }
    bool hasSource() const { return !m_path.empty(); }
private:
    bool m_valid = true; // 未指定文件时允许从系统模块中查找。
    std::string m_path;
    std::map<std::string,KSymInfo> m_infos;
};
