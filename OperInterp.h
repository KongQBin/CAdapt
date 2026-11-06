#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include "ElfPtrs.h"

// ELF 动态链接器（PT_INTERP）适配。
//
// 等长或更短的新路径：原地改写 PT_INTERP，剩余字节补零。
// 更长的新路径：把新串追加到文件末尾，PT_INTERP 与 .interp 节区改指
// 向新位置；其余内容与全部偏移保持不动。先写同目录临时文件，验证
// 通过后再原子 rename 替换；重写会换 inode，符号链接目标会被拒绝。
// 注意：.interp 节区的 sh_addr 仍指向旧位置（只改 sh_offset/sh_size），
// 以 PT_INTERP 为准。
class OperInterp
{
public:
    OperInterp() = default;
    ~OperInterp() = default;

    // 修改单个文件或目录下所有带 PT_INTERP 的 ELF。dryRun 只打印将要做的
    // 修改，不写文件。目录中无 PT_INTERP 的文件会被跳过，不计为失败；
    // 但只要有一个带 PT_INTERP 的文件修改失败，整体返回 false。
    bool adaptTargets(const std::string& path, const std::string& newInterp, bool dryRun);
    // 打印单个文件或目录下各文件的当前解释器；无 PT_INTERP 返回 false。
    bool showTargets(const std::string& path);

private:
    // 1: 有 PT_INTERP，0: 有效 ELF 但无 PT_INTERP，-1: 不是有效 ELF。
    int hasInterpreter(const std::string& path) const;

    bool setInterpreterForFile(const std::string& path, const std::string& newInterp, bool dryRun);
    // 更长：重写文件；调用前已校验新解释器。失败时原文件保持不变。
    bool rewriteWithLongerInterpreter(const std::string& path, const std::string& newInterp, const std::string& oldInterp);
    // 写盘后重读验证：PT_INTERP 与预期一致，且各 LOAD 段偏移与虚址对 p_align 同余。
    bool verifyInterpreter(const std::string& path, const std::string& expected) const;
    bool showInterpreterForFile(const std::string& path) const;
    // 写之前校验新解释器：绝对路径、存在且为普通文件、64 位小端 ET_DYN、
    // 与目标 e_machine 一致、本身没有 PT_INTERP（真正的 ld.so 不需要解释器）。
    bool validateNewInterpreter(const std::string& newInterp, Elf64_Half targetMachine) const;
};
