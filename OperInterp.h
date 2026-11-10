#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include "ElfPtrs.h"
#include "ElfUtils.h"

// ELF 动态链接器（PT_INTERP）适配。
//
// 等长或更短的新路径：原地改写 PT_INTERP，剩余字节补零。
// 更长的新路径：把新串追加到文件末尾，PT_INTERP 与 .interp 节区改指
// 向新位置；其余内容与全部偏移保持不动。先写同目录临时文件，验证
// 通过后再原子 rename 替换；重写会换 inode，符号链接目标会被拒绝。
// 注意：.interp 节区的 sh_addr 仍指向旧位置（只改 sh_offset/sh_size），
// 以 PT_INTERP 为准。
//
// 32 位支持：核心逻辑按 ELF 类别模板化（Elf32Traits/Elf64Traits），
// 目标文件按类别分发；可同时指定多个解释器（如 32 位与 64 位各一
// 个），每个目标按类别、字节序、机器架构挑选匹配的候选，不匹配时
// 警告并跳过该文件；大端文件暂不支持，警告并跳过。
class OperInterp
{
public:
    OperInterp() = default;
    ~OperInterp() = default;

    // 修改单个文件或目录下所有带 PT_INTERP 的 ELF。dryRun 只打印将要做的
    // 修改，不写文件。目录中无 PT_INTERP 的文件会被跳过，不计为失败；
    // 但只要有一个带 PT_INTERP 的文件修改失败，整体返回 false。
    // newInterps 可指定多个解释器（按命令行顺序优先匹配）。
    bool adaptTargets(const std::string& path, const std::vector<std::string>& newInterps, bool dryRun);
    // 打印单个文件或目录下各文件的当前解释器；无 PT_INTERP 返回 false。
    bool showTargets(const std::string& path);

private:
    // 单个文件：识别类别后分发到对应模板实例。
    FileAdaptResult adaptOneFile(const std::string& path, const std::vector<std::string>& newInterps, bool dryRun);
    FileAdaptResult showOneFile(const std::string& path) const;

    // 1: 有 PT_INTERP，0: 有效 ELF 但无 PT_INTERP，-1: 不是有效 ELF。
    template <typename Traits>
    int hasInterpreter(const std::string& path) const;

    template <typename Traits>
    FileAdaptResult adaptOneFileFor(const std::string& path, const std::vector<std::string>& newInterps, bool dryRun);
    template <typename Traits>
    FileAdaptResult showOneFileFor(const std::string& path) const;
    // 从候选中挑出第一个与目标身份（类别/字节序/架构）匹配且校验通过
    // 的解释器；无则返回 false。
    template <typename Traits>
    bool selectInterpreter(const std::vector<std::string>& candidates, typename Traits::Half targetMachine, std::string& chosen) const;

    template <typename Traits>
    bool setInterpreterForFile(const std::string& path, const std::string& newInterp, bool dryRun);
    // 更长：重写文件；调用前已校验新解释器。失败时原文件保持不变。
    template <typename Traits>
    bool rewriteWithLongerInterpreter(const std::string& path, const std::string& newInterp, const std::string& oldInterp);
    // 写盘后重读验证：PT_INTERP 与预期一致，且各 LOAD 段偏移与虚址对 p_align 同余。
    template <typename Traits>
    bool verifyInterpreter(const std::string& path, const std::string& expected) const;
    template <typename Traits>
    bool showInterpreterForFile(const std::string& path) const;
    // 写之前校验新解释器：绝对路径、存在且为普通文件、与目标同类别的
    // 小端 ET_DYN、与目标 e_machine 一致、本身没有 PT_INTERP
    // （真正的 ld.so 不需要解释器）。
    template <typename Traits>
    bool validateNewInterpreter(const std::string& newInterp, typename Traits::Half targetMachine) const;
};
