#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include "ElfPtrs.h"
#include "ElfUtils.h"
#include "ElfRewrite.h"

// ELF 运行库搜索路径（DT_RPATH / DT_RUNPATH）适配。
//
// 等长或更短的新路径：新串原地写进 .dynstr，剩余字节补零。
// 更长的新路径：走 patchelf 式文件重写（见 ElfRewrite）——新 .dynstr
// 追加到文件末尾的新 LOAD 段里，DT_STRTAB/DT_STRSZ 与 RPATH 条目的
// d_val 按新位置重算；其余内容与全部偏移保持不动。没有
// DT_RPATH/DT_RUNPATH 的目标同样走重写：.dynamic 增长一个
// Elf_Dyn 槽位并在最前面插入一条新的 DT_RUNPATH，同时把新串追加到
// .dynstr。静态文件（无 PT_DYNAMIC）无法设置 RPATH。
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

    // 修改单个文件或目录下所有动态 ELF 的 RPATH。dryRun 只打印将要做的
    // 修改，不写文件。目录中不适用 RPATH 的文件会被跳过，不计为失败；
    // 但只要有一个适用文件修改失败，整体返回 false。
    bool adaptTargets(const std::string& path, const std::string& newRpath, bool dryRun);
    // 打印单个文件或目录下各文件的当前 RPATH；无 RPATH 返回 false。
    bool showTargets(const std::string& path);

private:
    // 目标的 RPATH 状态：无效 ELF / 有效但无动态段 / 有动态段但无
    // RPATH / 有 RPATH。
    enum class RpathState { Invalid, NoDynamic, NoEntry, HasEntry };

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

    template <typename Traits>
    RpathState rpathState(const std::string& path) const;
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
    // 更长的新路径或无 RPATH 的目标：走文件重写。调用前已校验语法、
    // 排除 no-op；失败时原文件保持不变。
    template <typename Traits>
    bool rewriteWithRpath(const std::string& path, const std::string& newRpath,
                          const RpathEntry<Traits>* entry);
    // 在 .dynamic 最前面插入一条 DT_RUNPATH（值 = 新串在 .dynstr 中的
    // 偏移），其余条目下移一位，DT_NULL 仍在末尾。DT_MIPS_RLD_MAP_REL
    // 的值与自身槽位地址相关，存在时拒绝（无法随 .dynamic 一起搬家）。
    template <typename Traits>
    bool buildDynamicWithRpath(const std::string& oldDynamic,
                               typename Traits::Xword newStringOffset,
                               std::string& newDynamic) const;
    // 写盘后重读验证：解析出的值与预期一致。
    template <typename Traits>
    bool verifyRpath(const std::string& path, const std::string& expected) const;
};
