#pragma once
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <string>

// 检测当前语言环境
inline bool is_chinese_locale()
{
    const char* env_vars[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
    for (const char* var : env_vars)
    {
        const char* val = std::getenv(var);
        if (val && std::strlen(val) > 0)
        {
            if (std::string(val).find("zh") != std::string::npos) {
                return true;
            }
            return false;
        }
    }
    return false;
}

// 打印帮助
inline void print_help(const char* prog_name)
{
    std::cout << "------------------------->Help Info<-------------------------" << std::endl;
    std::cout << "Usage: " << prog_name << " [Options]" << std::endl;
    std::cout << std::endl;

    if (is_chinese_locale())
    {
        std::cout << "【GLIBC 适配选项】" << std::endl;
        std::cout << "  -c, --libc <path>     必需，可重复指定。指定提供符号表的GLIBC(libc.so.6)文件位置；可同时给多个" << std::endl;
        std::cout << "                        （如 32 位与 64 位各一个），每个目标按类别、字节序、机器架构挑选匹配的。" << std::endl;
        std::cout << "  -t, --target <path>   必需。指定要适配GLIBC版本及符号表的ELF文件/所在目录。" << std::endl;
        std::cout << std::endl;
        std::cout << "【动态链接器(解释器)适配选项】" << std::endl;
        std::cout << "  -i, --interp <path>   必需(与 -p 二选一)，可重复指定。指定新的动态链接器绝对路径；可同时给多个" << std::endl;
        std::cout << "                        （如 32 位与 64 位各一个），每个目标按类别、字节序、机器架构挑选匹配的。" << std::endl;
        std::cout << "                        等长或更短的路径原地改写；更长路径会重写文件：新串追加到文件末尾，PT_INTERP 改指新位置。" << std::endl;
        std::cout << "  -p, --print-interp    只查看目标的当前解释器，不修改文件。" << std::endl;
        std::cout << "      --dry-run         与 -i 联用时只打印将要做的修改，不写文件。" << std::endl;
        std::cout << "  -t, --target <path>   必需。指定要修改解释器的ELF文件/所在目录。" << std::endl;
        std::cout << std::endl;
        std::cout << "【RPATH 适配选项】" << std::endl;
        std::cout << "  -r, --rpath <path>    必需(与 --print-rpath 二选一)。新的运行库搜索路径，冒号分隔；空串表示清空。" << std::endl;
        std::cout << "                        等长或更短的新路径原地改写；更长路径会重写文件：新 .dynstr 追加到文件末尾的新 LOAD 段里。" << std::endl;
        std::cout << "                        无 RPATH 的目标会新增一条 DT_RUNPATH（.dynamic 增长一个槽位）。" << std::endl;
        std::cout << "                        同时存在 DT_RPATH 与 DT_RUNPATH 时优先改 RUNPATH。" << std::endl;
        std::cout << "      --print-rpath     只查看目标的当前 RPATH，不修改文件。" << std::endl;
        std::cout << "      --dry-run         与 -r 联用时只打印将要做的修改，不写文件。" << std::endl;
        std::cout << "  -t, --target <path>   必需。指定要修改 RPATH 的ELF文件/所在目录。" << std::endl;
        std::cout << std::endl;
        std::cout << "【内核模块(ko) 适配选项】" << std::endl;
        std::cout << "  -m, --kmod <path>     必需。指定要修改的内核模块(.ko)文件位置。" << std::endl;
        std::cout << "  -s, --symvers <path>  可选。指定内核符号版本文件(Module.symvers)的位置。若不指定，将从当前运行系统中提取。" << std::endl;
        std::cout << "  -v, --vermagic <str>  可选。指定要修改的目标版本魔术(vermagic)字符串。若不指定，将从当前运行系统中提取。" << std::endl;
        std::cout << std::endl;
        std::cout << "【通用选项】" << std::endl;
        std::cout << "  -h, --help            显示此帮助信息。" << std::endl;
        std::cout << std::endl;
        std::cout << "【说明】" << std::endl;
        std::cout << "  支持 32 位与 64 位小端 ELF；目标目录可同时包含两种文件，各自匹配对应的 libc/解释器。" << std::endl;
        std::cout << "  指定的 libc/解释器与目标不匹配时警告并跳过该文件；大端 ELF 暂不支持，警告并跳过。" << std::endl;
        std::cout << "  内核模块同理：32 位模块的 __versions 条目用 4 字节 CRC；目标模块架构与当前系统不一致、" << std::endl;
        std::cout << "  而符号 CRC 或 vermagic 又取自当前系统时，警告并拒绝修改。" << std::endl;
        std::cout << std::endl;
        std::cout << "Example:" << std::endl;
        std::cout << "  GLIBC适配: " << prog_name << " -c /lib64/libc.so.6 -t ./my_program" << std::endl;
        std::cout << "  混合适配: " << prog_name << " -c /lib/libc.so.6 -c /lib64/libc.so.6 -t ./mixed_dir" << std::endl;
        std::cout << "  解释器适配: " << prog_name << " -i /lib64/ld-linux-x86-64.so.2 -t ./my_program" << std::endl;
        std::cout << "  混合解释器: " << prog_name << " -i /lib/ld-linux.so.2 -i /lib64/ld-linux-x86-64.so.2 -t ./mixed_dir" << std::endl;
        std::cout << "  查看解释器: " << prog_name << " -p -t ./my_program" << std::endl;
        std::cout << "  RPATH适配: " << prog_name << " -r /opt/my-libs -t ./my_program" << std::endl;
        std::cout << "  查看RPATH: " << prog_name << " --print-rpath -t ./my_program" << std::endl;
        std::cout << "  KMOD适配:  " << prog_name << " -m ./my_driver.ko -s ./Module.symvers -v \"4.18.0-193.el8.x86_64 SMP mod_unload\"" << std::endl;
    }
    else
    {
        std::cout << "[GLIBC Adaptation Options]" << std::endl;
        std::cout << "  -c, --libc <path>     Required, repeatable. Path to the GLIBC (libc.so.6) file providing the symbol table;" << std::endl;
        std::cout << "                        several may be given (e.g. one 32-bit and one 64-bit); each target picks the" << std::endl;
        std::cout << "                        matching one by class, endianness and machine." << std::endl;
        std::cout << "  -t, --target <path>   Required. Path to the ELF file or directory to adapt GLIBC versions and symbols." << std::endl;
        std::cout << std::endl;
        std::cout << "[Dynamic Linker (Interpreter) Adaptation Options]" << std::endl;
        std::cout << "  -i, --interp <path>   Required (either -i or -p), repeatable. Absolute path of the new dynamic linker;" << std::endl;
        std::cout << "                        several may be given (e.g. one 32-bit and one 64-bit); each target picks the" << std::endl;
        std::cout << "                        matching one by class, endianness and machine." << std::endl;
        std::cout << "                        A path of equal or shorter length is patched in place; a longer path rewrites the file: the new string is appended at the end and PT_INTERP is repointed to it." << std::endl;
        std::cout << "  -p, --print-interp    Show the target's current interpreter without modifying the file." << std::endl;
        std::cout << "      --dry-run         With -i, only print the planned change without writing the file." << std::endl;
        std::cout << "  -t, --target <path>   Required. Path to the ELF file or directory whose interpreter is to be changed." << std::endl;
        std::cout << std::endl;
        std::cout << "[RPATH Adaptation Options]" << std::endl;
        std::cout << "  -r, --rpath <path>    Required (either -r or --print-rpath). New library search path, colon-separated; an empty string clears it." << std::endl;
        std::cout << "                        An equal or shorter path is patched in place; a longer path rewrites the file: the new .dynstr is appended in a new PT_LOAD segment at the end of the file." << std::endl;
        std::cout << "                        A target without RPATH gains a new DT_RUNPATH entry (.dynamic grows by one slot)." << std::endl;
        std::cout << "                        When both DT_RPATH and DT_RUNPATH exist, RUNPATH is modified." << std::endl;
        std::cout << "      --print-rpath     Show the target's current RPATH without modifying the file." << std::endl;
        std::cout << "      --dry-run         With -r, only print the planned change without writing the file." << std::endl;
        std::cout << "  -t, --target <path>   Required. Path to the ELF file or directory whose RPATH is to be changed." << std::endl;
        std::cout << std::endl;
        std::cout << "[Kernel Module (.ko) Adaptation Options]" << std::endl;
        std::cout << "  -m, --kmod <path>     Required. Path to the target kernel module (.ko) file to modify." << std::endl;
        std::cout << "  -s, --symvers <path>  Optional. Path to the Module.symvers file. If not specified, extracts from the current running system." << std::endl;
        std::cout << "  -v, --vermagic <str>  Optional. Target vermagic string to modify. If not specified, extracts from the current running system." << std::endl;
        std::cout << std::endl;
        std::cout << "[General Options]" << std::endl;
        std::cout << "  -h, --help            Show this help message." << std::endl;
        std::cout << std::endl;
        std::cout << "[Notes]" << std::endl;
        std::cout << "  Both 32-bit and 64-bit little-endian ELF files are supported; a target directory may contain" << std::endl;
        std::cout << "  both, each matching its own libc/interpreter. A target with no matching libc/interpreter is" << std::endl;
        std::cout << "  warned about and skipped; big-endian ELF files are not yet supported and are warned about and skipped." << std::endl;
        std::cout << "  Kernel modules follow the same rule: 32-bit modules use 4-byte CRCs in __versions; when the" << std::endl;
        std::cout << "  target module's machine differs from the running system while symbol CRCs or vermagic come" << std::endl;
        std::cout << "  from that system, the change is warned about and refused." << std::endl;
        std::cout << std::endl;
        std::cout << "Example:" << std::endl;
        std::cout << "  GLIBC Adaptation: " << prog_name << " -c /lib64/libc.so.6 -t ./my_program" << std::endl;
        std::cout << "  Mixed Adaptation: " << prog_name << " -c /lib/libc.so.6 -c /lib64/libc.so.6 -t ./mixed_dir" << std::endl;
        std::cout << "  Interpreter Adaptation: " << prog_name << " -i /lib64/ld-linux-x86-64.so.2 -t ./my_program" << std::endl;
        std::cout << "  Mixed Interpreters: " << prog_name << " -i /lib/ld-linux.so.2 -i /lib64/ld-linux-x86-64.so.2 -t ./mixed_dir" << std::endl;
        std::cout << "  Show Interpreter: " << prog_name << " -p -t ./my_program" << std::endl;
        std::cout << "  RPATH Adaptation: " << prog_name << " -r /opt/my-libs -t ./my_program" << std::endl;
        std::cout << "  Show RPATH: " << prog_name << " --print-rpath -t ./my_program" << std::endl;
        std::cout << "  KMOD Adaptation:  " << prog_name << " -m ./my_driver.ko -s ./Module.symvers -v \"4.18.0-193.el8.x86_64 SMP mod_unload\"" << std::endl;
    }
}
