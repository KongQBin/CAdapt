#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include "ElfPtrs.h"
#include "ElfUtils.h"

// ELF 运行库搜索路径（DT_RPATH / DT_RUNPATH）适配。
//
// 只支持等长或更短的新路径：新串原地写进 .dynstr，剩余字节补零。
// 更长的新路径会被拒绝且不修改文件：RPATH 串由加载器按 DT_STRTAB +
// 偏移从已映射内存读取，无法像 PT_INTERP 那样追加到文件末尾；要容纳
// 更长的串需要重排整个文件并新增 LOAD 段（patchelf 的做法），超出本
// 次范围。没有 DT_RPATH/DT_RUNPATH 的目标同样会被拒绝（动态段一般没
// 有空闲槽位可供新增条目）。
// 若同时存在两项，优先改 DT_RUNPATH（现代链接器默认），只读查看时两
// 项都会打印；空字符串表示清空。
//
// 32 位支持：核心逻辑按 ELF 类别模板化（Elf32Traits/Elf64Traits），
// 目标文件按类别分发；RPATH 值本身与类别无关，无需多候选。大端文件
// 暂不支持，警告并跳过。
class OperRpath
{
public:
    OperRpath() = default;
    ~OperRpath() = default;

    // 修改单个文件或目录下所有带 RPATH 的 ELF。dryRun 只打印将要做的
    // 修改，不写文件。目录中无 RPATH 的文件会被跳过，不计为失败；但
    // 只要有一个带 RPATH 的文件修改失败，整体返回 false。
    bool adaptTargets(const std::string& path, const std::string& newRpath, bool dryRun);
    // 打印单个文件或目录下各文件的当前 RPATH；无 RPATH 返回 false。
    bool showTargets(const std::string& path);

private:
    // .dynamic 中 RPATH/RUNPATH 条目的解析结果。
    template <typename Traits>
    struct RpathEntry
    {
        typename Traits::Word tag;        // DT_RPATH 或 DT_RUNPATH
        typename Traits::Xword strOffset; // 串在 .dynstr 中的偏移（DT 条目的 d_val）
        typename Traits::Xword fileOffset;// 串在文件中的偏移
        std::string value;                // 当前 RPATH 值（不含结尾 NUL）
    };

    // 单个文件：识别类别后分发到对应模板实例。
    FileAdaptResult adaptOneFile(const std::string& path, const std::string& newRpath, bool dryRun);
    FileAdaptResult showOneFile(const std::string& path) const;

    // 1: 有 RPATH，0: 有效 ELF 但无 RPATH，-1: 不是有效 ELF。
    template <typename Traits>
    int hasRpath(const std::string& path) const;
    template <typename Traits>
    FileAdaptResult adaptOneFileFor(const std::string& path, const std::string& newRpath, bool dryRun);
    template <typename Traits>
    FileAdaptResult showOneFileFor(const std::string& path) const;
    // 解析优先改的那一项（有 RUNPATH 改 RUNPATH，否则改 RPATH）；无则 false。
    template <typename Traits>
    bool resolveEntry(ElfPtrs<Traits>& ptrs, RpathEntry<Traits>& entry) const;
    // 解析所有 RPATH/RUNPATH 条目（RUNPATH 在前）；无则 false。
    template <typename Traits>
    bool resolveEntries(ElfPtrs<Traits>& ptrs, std::vector<RpathEntry<Traits>>& entries) const;
    // 校验新 RPATH 语法：空串表示清空；每段须为绝对路径或以 $ 开头
    //（$ORIGIN/$LIB/$PLATFORM 等加载器变量）。
    bool validateNewRpath(const std::string& newRpath) const;

    template <typename Traits>
    bool setRpathForFile(const std::string& path, const std::string& newRpath, bool dryRun);
    template <typename Traits>
    bool showRpathForFile(const std::string& path) const;
    // 写盘后重读验证：解析出的值与预期一致。
    template <typename Traits>
    bool verifyRpath(const std::string& path, const std::string& expected) const;
};
