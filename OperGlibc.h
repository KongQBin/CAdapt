#pragma once
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include <memory>
#include "ElfPtrs.h"

struct GlibcVersionInfo
{
    std::string name;
    int id;
    std::vector<std::string> symbols;
};

struct TargetVersionInfo
{
    std::string name;
    int id;
};

struct SymbolPatchInfo
{
    int symbolIndex;
    int originalTargetVersionIndex;
    std::string hostVersionName;
    int hostVersionId;
};

class OperGlibc
{
public:
    OperGlibc() = default;
    ~OperGlibc() = default;
    void initGlibcInfo(const std::string& glibcPath);
    void showGlibcInfo(bool showDynsym = false) const;
    void adaptedTargets(const std::string& path);
    void clearContainer();

private:
    void getAllElf(const char *dir);
    bool isElf(const char *file) const;

    bool adaptedTargetElfFileGlibcVersion(const std::string& path);
    bool checkFoundDynsym();

    std::pair<std::string, int> containsVersion(const std::string& version) const;
    std::pair<std::string, int> containsDynsym(const std::string& dynsym) const;

    std::unique_ptr<ElfPtrs> glibcPtrs;
    std::unique_ptr<ElfPtrs> targetElfPtrs;

    Elf64_Verneed *targetElfLibcVerneed = nullptr;
    int targetMaxPathLen = 0;

    std::vector<std::string> targetElf_vct;
    std::vector<TargetVersionInfo> targetVersionInfo_vct;
    std::vector<SymbolPatchInfo> validIndexAndId_vct;
    std::vector<GlibcVersionInfo> glibcVersionInfo_vct;
};
