#pragma once
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include "ElfPtrs.h"

using namespace std;
class GlibcOper
{
public:
    GlibcOper();
    ~GlibcOper();
    void initGlibcInfo(string glibcPath);
    void showGlibcInfo(bool showDynsym = false);
    void adaptedTargets(string path);
    void clearContainer();
private:
    void getAllElf(const char *dir);
    bool isElf(const char *file);
    bool adaptedTargetElfFileGlibcVersion(string path);
    bool checkFountDynsym();
    pair<string,int> containsVersion(string version);
    pair<string,int> containsDynsym(string dynsym);
    ElfPtrs *glibcPtrs = NULL;
    ElfPtrs *targetElfPtrs = NULL;
    Elf64_Verneed *targetElfLibcVerneed = NULL;
    int targetMaxPathLen = 0;
    vector<string>  targetElf_vct;
    vector<pair<string,int> > targetVersionInfo_vct;
    //符号索引  版本索引    本地支持的GLIBC版本    本地支持的版本对应当前elf文件中的索引
    vector<pair<pair<int,int>,pair<string, int> > > validIndexAndId_vct;
    //版本  版本索引  对应的符号表
    vector<pair<pair<string,int>,vector<string> > > glibcVersionInfo_vct;
};
