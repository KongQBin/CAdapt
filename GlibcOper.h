#pragma once
#include <iostream>
#include <string>
#include <utility> // for pair
#include <vector>
#include <memory>  // C++11 for unique_ptr
#include "ElfPtrs.h"

// C++98/11 struct
struct GlibcVersionInfo
{
    string name;
    int id;
    vector<string> symbols;
};

// C++98/11 struct
struct TargetVersionInfo
{
    string name;
    int id;
};

// C++98/11 struct
struct SymbolPatchInfo
{
    int symbolIndex; 
    int originalTargetVersionIndex;
    string hostVersionName;
    int hostVersionId; 
};


class GlibcOper
{
public:
    GlibcOper() = default; // C++11
    ~GlibcOper() = default; // C++11 (unique_ptr 会自动处理)
    void initGlibcInfo(const string& glibcPath);
    void showGlibcInfo(bool showDynsym = false) const; 
    void adaptedTargets(const string& path);
    void clearContainer();

private:
    // 还原为 C-Style (C++11 兼容)
    void getAllElf(const char *dir); 
    bool isElf(const char *file) const;
    
    bool adaptedTargetElfFileGlibcVersion(const string& path);
    bool checkFoundDynsym(); // 修正拼写

    pair<string, int> containsVersion(const string& version) const;
    pair<string, int> containsDynsym(const string& dynsym) const;

    // C++11 智能指针
    unique_ptr<ElfPtrs> glibcPtrs;
    unique_ptr<ElfPtrs> targetElfPtrs;
    
    Elf64_Verneed *targetElfLibcVerneed = nullptr; // C++11
    int targetMaxPathLen = 0;
    
    vector<string> targetElf_vct;
    vector<TargetVersionInfo> targetVersionInfo_vct;
    vector<SymbolPatchInfo> validIndexAndId_vct;
    vector<GlibcVersionInfo> glibcVersionInfo_vct;
};
