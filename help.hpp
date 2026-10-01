#pragma once
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <string>

// 检测当前语言环境
bool is_chinese_locale()
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
void print_help(const char* prog_name)
{
    std::cout << "------------------------->Help Info<-------------------------" << std::endl;
    std::cout << "Usage: " << prog_name << " [Options]" << std::endl;
    std::cout << std::endl;

    if (is_chinese_locale())
    {
        std::cout << "【GLIBC 适配选项】" << std::endl;
        std::cout << "  -c, --libc <path>     必需。指定提供符号表的GLIBC(libc.so.6)文件位置。" << std::endl;
        std::cout << "  -t, --target <path>   必需。指定要适配GLIBC版本及符号表的ELF文件/所在目录。" << std::endl;
        std::cout << std::endl;
        std::cout << "【内核模块(ko) 适配选项】" << std::endl;
        std::cout << "  -m, --kmod <path>     必需。指定要修改的内核模块(.ko)文件位置。" << std::endl;
        std::cout << "  -s, --symvers <path>  可选。指定内核符号版本文件(Module.symvers)的位置。若不指定，将从当前运行系统中提取。" << std::endl;
        std::cout << "  -v, --vermagic <str>  可选。指定要修改的目标版本魔术(vermagic)字符串。若不指定，将从当前运行系统中提取。" << std::endl;
        std::cout << std::endl;
        std::cout << "【通用选项】" << std::endl;
        std::cout << "  -h, --help            显示此帮助信息。" << std::endl;
        std::cout << std::endl;
        std::cout << "Example:" << std::endl;
        std::cout << "  GLIBC适配: " << prog_name << " -c /lib64/libc.so.6 -t ./my_program" << std::endl;
        std::cout << "  KMOD适配:  " << prog_name << " -m ./my_driver.ko -s ./Module.symvers -v \"4.18.0-193.el8.x86_64 SMP mod_unload\"" << std::endl;
    }
    else
    {
        std::cout << "[GLIBC Adaptation Options]" << std::endl;
        std::cout << "  -c, --libc <path>     Required. Path to the GLIBC (libc.so.6) file providing the symbol table." << std::endl;
        std::cout << "  -t, --target <path>   Required. Path to the ELF file or directory to adapt GLIBC versions and symbols." << std::endl;
        std::cout << std::endl;
        std::cout << "[Kernel Module (.ko) Adaptation Options]" << std::endl;
        std::cout << "  -m, --kmod <path>     Required. Path to the target kernel module (.ko) file to modify." << std::endl;
        std::cout << "  -s, --symvers <path>  Optional. Path to the Module.symvers file. If not specified, extracts from the current running system." << std::endl;
        std::cout << "  -v, --vermagic <str>  Optional. Target vermagic string to modify. If not specified, extracts from the current running system." << std::endl;
        std::cout << std::endl;
        std::cout << "[General Options]" << std::endl;
        std::cout << "  -h, --help            Show this help message." << std::endl;
        std::cout << std::endl;
        std::cout << "Example:" << std::endl;
        std::cout << "  GLIBC Adaptation: " << prog_name << " -c /lib64/libc.so.6 -t ./my_program" << std::endl;
        std::cout << "  KMOD Adaptation:  " << prog_name << " -m ./my_driver.ko -s ./Module.symvers -v \"4.18.0-193.el8.x86_64 SMP mod_unload\"" << std::endl;
    }
}
