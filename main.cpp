#include <iostream>
#include <cstring>
#include <string>
#include <getopt.h>
#include "OperGlibc.h"
#include "OperKmod.h"

using namespace std;

/**
 * @brief 打印帮助信息
 * @param prog_name 程序名 (argv[0])
 */
void print_help(const char* prog_name)
{
    cout << "------------------------->Help Info<-------------------------" << endl;
    cout << "Usage: " << prog_name << " [Options]" << endl;
    cout << endl;
    cout << "【GLIBC 适配选项】" << endl;
    cout << "  -c, --libc <path>     必需。指定提供符号表的GLIBC(libc.so.6)文件位置。" << endl;
    cout << "  -t, --target <path>   必需。指定要适配GLIBC版本及符号表的ELF文件/所在目录。" << endl;
    cout << endl;
    cout << "【内核模块(ko) 适配选项】" << endl;
    cout << "  -m, --kmod <path>     必需。指定要修改的内核模块(.ko)文件位置。" << endl;
    cout << "  -s, --symvers <path>  可选。指定内核符号版本文件(Module.symvers)的位置。若不指定，将从当前运行系统中提取。" << endl;
    cout << "  -v, --vermagic <str>  可选。指定要修改的目标版本魔术(vermagic)字符串。若不指定，将从当前运行系统中提取。" << endl;
    cout << endl;
    cout << "【通用选项】" << endl;
    cout << "  -h, --help            显示此帮助信息。" << endl;
    cout << endl;
    cout << "Example:" << endl;
    cout << "  GLIBC适配: " << prog_name << " -c /lib64/libc.so.6 -t ./my_program" << endl;
    cout << "  KMOD适配:  " << prog_name << " -m ./my_driver.ko -s ./Module.symvers -v \"4.18.0-193.el8.x86_64 SMP mod_unload\"" << endl;
}

int main(int argc, char **argv)
{
    string libc_path;
    string target_path;
    string kmod_path;
    string symvers_path;
    string vermagic_str;
    bool show_help = false;

    static struct option long_options[] = {
        // name,       has_arg,           flag, val
        {"help",     no_argument,       0, 'h'},
        {"libc",     required_argument, 0, 'c'},
        {"target",   required_argument, 0, 't'},
        {"kmod",     required_argument, 0, 'm'},
        {"symvers",  required_argument, 0, 's'},
        {"vermagic", required_argument, 0, 'v'},
        {0, 0, 0, 0}
    };

    int opt = 0;
    int option_index = 0;

    // "hc:t:m:s:v:" 是短选项字符串，带有冒号的表示需要提供参数
    while ((opt = getopt_long(argc, argv, "hc:t:m:s:v:", long_options, &option_index)) != -1)
    {
        switch (opt) {
        case 'h':
            show_help = true;
            break;
        case 'c':
            libc_path = optarg;
            break;
        case 't':
            target_path = optarg;
            break;
        case 'm':
            kmod_path = optarg;
            break;
        case 's':
            symvers_path = optarg;
            break;
        case 'v':
            vermagic_str = optarg;
            break;
        default: // '?' 表示无法识别的选项或缺少参数
            show_help = true;
            break;
        }
    }

    if (optind < argc) {
        cerr << "错误：检测到未知的非选项参数。" << endl;
        show_help = true;
    }

    // 判断用户想要执行的任务类型
    bool run_glibc = !libc_path.empty() || !target_path.empty();
    bool run_kmod  = !kmod_path.empty();

    if (!show_help && !run_glibc && !run_kmod) {
        cerr << "错误：必须指定 GLIBC 适配参数(-c/-t) 或 内核模块适配参数(-m)。" << endl;
        show_help = true;
    }

    // 校验 GLIBC 参数
    if (!show_help && run_glibc) {
        if (libc_path.empty() || target_path.empty()) {
            cerr << "错误：对于GLIBC适配，--libc 和 --target 都是必需参数。" << endl;
            show_help = true;
        } else if (libc_path.find("libc.so") == string::npos) {
            cerr << "错误：--libc 参数 \"" << libc_path << "\" 似乎不是一个有效的 libc.so 文件。" << endl;
            show_help = true;
        }
    }

    if (show_help)
    {
        print_help(argv[0]);
        return (argc == 1) ? 0 : 1;
    }


    // 核心业务分发执行
    // ================== GLIBC 符号适配 ==================
    if (run_glibc) {
        cout << "============== GLIBC 适配 ==============" << endl;
        cout << "GLIBC 路径: " << libc_path << endl;
        cout << "目标 路径: " << target_path << endl;

        OperGlibc localVersion;
        localVersion.initGlibcInfo(libc_path);
        localVersion.adaptedTargets(target_path);

        cout << "GLIBC 适配完成。" << endl;
    }

    // ================== 内核模块(KO) 适配 ==================
    if (run_kmod) {
        if (run_glibc) cout << endl; // 空行分隔
        cout << "============== 内核模块 适配 ==============" << endl;
        cout << "目标模块: " << kmod_path << endl;
        if (!symvers_path.empty()) cout << "符号文件(Symvers): " << symvers_path << endl;
        if (!vermagic_str.empty()) cout << "目标版本魔术(Vermagic): " << vermagic_str << endl;

        // 根据是否提供了 Module.symvers 文件选择不同的构造函数
        ElfModify* modifier = nullptr;
        if (!symvers_path.empty()) {
            modifier = new ElfModify(symvers_path, kmod_path);
        } else {
            modifier = new ElfModify(kmod_path);
        }

        if (modifier->motify(vermagic_str)) {
            cout << "内核模块适配成功。" << endl;
        } else {
            cerr << "内核模块适配过程中存在失败项，请检查日志。" << endl;
        }
        delete modifier;
    }

    cout << "--------------------------------------------------------" << endl;
    cout << "任务结束。" << endl;

    return 0;
}
