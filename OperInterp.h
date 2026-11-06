#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include "ElfPtrs.h"

// ELF 动态链接器（PT_INTERP）适配，第一阶段。
//
// 只支持等长或更短的新解释器路径：新路径（含结尾 NUL）必须能放进原
// PT_INTERP 段，剩余字节补零。更长路径需要重写整个文件（移位并修正
// 全部偏移），本阶段明确拒绝，见 README。
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
    bool getAllElf(const char *dir);
    int isElf(const char *file) const; // 1: ELF，0: 非ELF，-1: 读取失败。
    // 1: 有 PT_INTERP，0: 有效 ELF 但无 PT_INTERP，-1: 不是有效 ELF。
    int hasInterpreter(const std::string& path) const;

    bool setInterpreterForFile(const std::string& path, const std::string& newInterp, bool dryRun);
    bool showInterpreterForFile(const std::string& path) const;
    // 写之前校验新解释器：绝对路径、存在且为普通文件、64 位小端 ET_DYN、
    // 与目标 e_machine 一致、本身没有 PT_INTERP（真正的 ld.so 不需要解释器）。
    bool validateNewInterpreter(const std::string& newInterp, Elf64_Half targetMachine) const;

    std::vector<std::string> targetElf_vct;
};
