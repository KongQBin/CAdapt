#pragma once
#include <string>
#include <vector>

// 各适配器共用的 ELF 文件收集与判断逻辑：静态工具类，不可实例化。
//
// 原先 OperGlibc、OperInterp、OperRpath 各自复制了一份目录扫描与 ELF
// 判断实现；抽到这里后，三处共用同一份逻辑，改一处即改全部。
class ElfUtils
{
public:
    ElfUtils() = delete;

    // 递归收集 dir 下所有 64 位小端 ELF 普通文件（跳过符号链接），追加到 out。
    // 扫描中出错返回 false，但已收集的部分仍保留在 out 中。
    static bool collectElfFiles(const std::string& dir, std::vector<std::string>& out);
    // 1: 64 位小端 ELF，0: 不是，-1: 读取失败。
    static int isElfFile(const char* file);
    // path 是否为目录（跟随符号链接）。
    static bool isDirectory(const std::string& path);
};
