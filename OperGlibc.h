#pragma once
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include <map>
#include <memory>
#include <unordered_map>
#include "ElfPtrs.h"
#include "ElfUtils.h"

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
    size_t symbolIndex;
    int originalTargetVersionIndex;
    std::string hostVersionName;
    int hostVersionId;
};

// 解析后的单个 libc：类别无关的数据（版本、版本 id、符号归属）。
// ElfPtrs 只在解析期间持有，解析完即释放；多候选时按路径缓存。
struct ParsedLibc
{
    std::vector<GlibcVersionInfo> versions;
    std::unordered_map<std::string, int> versionIds;
    std::unordered_map<std::string, std::vector<std::pair<std::string, int>>> symbolVersions;
};

// GLIBC 符号版本适配。
//
// 32 位支持：核心逻辑按 ELF 类别模板化（Elf32Traits/Elf64Traits），
// 目标文件按类别分发；可同时指定多个 libc（如 32 位与 64 位各一
// 个），每个目标按类别、字节序、机器架构挑选匹配的候选，不匹配时
// 警告并跳过该文件；大端文件暂不支持，警告并跳过。
class OperGlibc
{
public:
    OperGlibc() = default;
    ~OperGlibc() = default;

    // 修改单个文件或目录下所有目标 ELF 的 GLIBC 符号版本。
    // libcPaths 可指定多个 libc（按命令行顺序优先匹配）。
    // 目录中没有 libc.so.6 依赖的目标会使整体返回 false；大端或
    // 无匹配 libc 的文件会被警告并跳过，不计为失败。
    bool adaptedTargets(const std::string& path, const std::vector<std::string>& libcPaths);

private:
    // 单个文件：识别类别后分发到对应模板实例。
    FileAdaptResult adaptOneFile(const std::string& path, const std::vector<std::string>& libcPaths);
    template <typename Traits>
    FileAdaptResult adaptOneFileFor(const std::string& path, const std::vector<std::string>& libcPaths);
    // 解析一个 libc（按路径缓存，重复目标共用同一份解析结果）。
    template <typename Traits>
    bool parseLibc(const std::string& glibcPath, ParsedLibc& out);
    template <typename Traits>
    bool adaptTargetFile(const std::string& path, const ParsedLibc& libc);
    // 检查目标的 .dynsym/.gnu.version，填好要删的 verneed 项与要改的
    // 版本编号；libcVerneed 指向目标映射内的 Verneed 记录。
    template <typename Traits>
    bool checkTarget(ElfPtrs<Traits>& targetPtrs, const ParsedLibc& libc,
                     typename Traits::Verneed*& libcVerneed,
                     std::vector<TargetVersionInfo>& targetVersions,
                     std::vector<SymbolPatchInfo>& patches);

    static void showGlibcInfo(const ParsedLibc& libc, bool showDynsym = false);
    static std::pair<std::string, int> findVersion(const ParsedLibc& libc, const std::string& version);
    static std::pair<std::string, int> findSymbol(const ParsedLibc& libc, const std::string& dynsym,
                                                  const std::string& preferredVersion,
                                                  const std::vector<TargetVersionInfo>& targetVersions);

    std::map<std::string, ParsedLibc> m_libcCache;
};
