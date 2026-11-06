#pragma once
#include <string>
#include <vector>

// 各适配器共用的 ELF 文件收集与判断逻辑：静态工具类，不可实例化。
//
// 原先 OperGlibc、OperInterp、OperRpath 各自复制了一份目录扫描与 ELF
// 判断实现；抽到这里后，三处共用同一份逻辑，改一处即改全部。
//
// 32 位与多候选支持：目录可能同时包含 32/64 位、大端/小端 ELF；
// identifyElf 只读文件头前 20 字节做轻量识别，供各适配器按目标身份
// 挑选匹配的候选（libc/解释器），不匹配时警告并跳过该文件。

// 单个文件一次适配的结果：已处理 / 跳过（不计失败）/ 失败。
enum class FileAdaptResult { Patched, Skipped, Failed };

// ELF 身份：类别（32/64 位）、字节序、机器架构。
struct ElfIdentity
{
    unsigned char elfClass = 0; // ELFCLASS32 / ELFCLASS64
    unsigned char elfData = 0;  // ELFDATA2LSB / ELFDATA2MSB
    unsigned short machine = 0; // e_machine，已按字节序还原
};

class ElfUtils
{
public:
    ElfUtils() = delete;

    // 递归收集 dir 下所有 ELF 普通文件（32/64 位、小端/大端均收；
    // 跳过符号链接），追加到 out。扫描中出错返回 false，但已收集的
    // 部分仍保留在 out 中。大端文件由各适配器识别后警告并跳过。
    static bool collectElfFiles(const std::string& dir, std::vector<std::string>& out);
    // 1: ELF（32/64 位、小端/大端），0: 不是，-1: 读取失败。
    static int isElfFile(const char* file);
    // path 是否为目录（跟随符号链接）。
    static bool isDirectory(const std::string& path);

    // 轻量识别：读文件头前 20 字节得到类别、字节序与机器架构。
    // 不是 ELF 或读不到时返回 false。
    static bool identifyElf(const std::string& path, ElfIdentity& out);
    // 身份的可读描述，如 "32 位小端 x86-64"。
    static std::string describeIdentity(const ElfIdentity& id);
    // e_machine 的可读名称，如 EM_X86_64 -> "x86-64"。
    static std::string machineName(unsigned short machine);
    // 大端文件暂不支持的统一警告（终端 + 错误日志）。
    static void warnUnsupportedEndian(const std::string& path, const ElfIdentity& id);
    // identifyElf 失败时调用：用 ElfPtrs 走一遍打开/校验，把“打开文件
    // 失败”“不是普通文件”“文件头无效”等精确原因记入错误日志（仅用
    // 于错误路径，文件此时已确认无法识别）。
    static void logIdentifyFailure(const std::string& path);
};
