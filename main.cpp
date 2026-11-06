#include <iostream>
#include <cstring>
#include <string>
#include <getopt.h>
#include "OperGlibc.h"
#include "OperKmod.h"
#include "OperInterp.h"
#include "help.hpp"

using namespace std;
int main(int argc, char **argv)
{
    string libc_path;
    string target_path;
    string kmod_path;
    string symvers_path;
    string vermagic_str;
    string interp_path;
    bool print_interp = false;
    bool dry_run = false;
    bool show_help = argc == 1;
    bool argument_error = false;

    static struct option long_options[] = {
        // name,       has_arg,           flag, val
        {"help",     no_argument,       0, 'h'},
        {"libc",     required_argument, 0, 'c'},
        {"target",   required_argument, 0, 't'},
        {"kmod",     required_argument, 0, 'm'},
        {"symvers",  required_argument, 0, 's'},
        {"vermagic", required_argument, 0, 'v'},
        {"interp",   required_argument, 0, 'i'},
        {"print-interp", no_argument,   0, 'p'},
        {"dry-run",  no_argument,       0, 1000},
        {0, 0, 0, 0}
    };

    int opt = 0;
    int option_index = 0;

    // "hc:t:m:s:v:i:p" 是短选项字符串，带有冒号的表示需要提供参数
    while ((opt = getopt_long(argc, argv, "hc:t:m:s:v:i:p", long_options, &option_index)) != -1)
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
        case 'i':
            interp_path = optarg;
            break;
        case 'p':
            print_interp = true;
            break;
        case 1000:
            dry_run = true;
            break;
        default:
            show_help = true;
            argument_error = true;
            break;
        }
    }

    if (optind < argc) {
        cerr << "错误：检测到未知的非选项参数。" << endl;
        show_help = true;
        argument_error = true;
    }

    // 判断用户想要执行的任务类型。-t 由 GLIBC 与解释器两种模式共用，
    // 因此不能仅凭 -t 推断为 GLIBC 模式；只给 -t 时在下面按缺少模式报错。
    bool run_glibc = !libc_path.empty();
    bool run_kmod  = !kmod_path.empty();
    bool run_interp = !interp_path.empty() || print_interp;

    if (!show_help && !run_glibc && !run_kmod && !run_interp) {
        cerr << "错误：必须指定 GLIBC 适配参数(-c/-t)、内核模块适配参数(-m) 或 解释器适配参数(-i/-p)。" << endl;
        show_help = true;
        argument_error = true;
    }

    // 校验 GLIBC 参数
    if (!show_help && run_glibc) {
        if (libc_path.empty() || target_path.empty()) {
            cerr << "错误：对于GLIBC适配，--libc 和 --target 都是必需参数。" << endl;
            show_help = true;
            argument_error = true;
        } else if (libc_path.find("libc.so") == string::npos) {
            cerr << "错误：--libc 参数 \"" << libc_path << "\" 似乎不是一个有效的 libc.so 文件。" << endl;
            show_help = true;
            argument_error = true;
        }
    }

    // 校验解释器参数
    if (!show_help && run_interp) {
        if (target_path.empty()) {
            cerr << "错误：对于解释器适配，--target 是必需参数。" << endl;
            show_help = true;
            argument_error = true;
        } else if (print_interp && !interp_path.empty()) {
            cerr << "错误：--print-interp 不能与 --interp 同时使用。" << endl;
            show_help = true;
            argument_error = true;
        } else if (print_interp && dry_run) {
            cerr << "错误：--print-interp 不能与 --dry-run 同时使用。" << endl;
            show_help = true;
            argument_error = true;
        } else if (!interp_path.empty() && interp_path[0] != '/') {
            cerr << "错误：--interp 参数 \"" << interp_path << "\" 必须是绝对路径。" << endl;
            show_help = true;
            argument_error = true;
        }
    }

    if (show_help)
    {
        print_help(argv[0]);
        return argument_error ? 1 : 0;
    }


    // 核心业务分发
    // ================== GLIBC 符号适配 ==================
    if (run_glibc) {
        cout << "============== GLIBC 适配 ==============" << endl;
        cout << "GLIBC 路径: " << libc_path << endl;
        cout << "目标 路径: " << target_path << endl;

        OperGlibc localVersion;
        localVersion.initGlibcInfo(libc_path);
        if (!localVersion.adaptedTargets(target_path)) {
            cerr << "GLIBC 适配失败，请检查日志。" << endl;
            return 1;
        }

        cout << "GLIBC 适配完成。" << endl;
    }

    // ================== 动态链接器(解释器)适配 ==================
    if (run_interp) {
        if (run_glibc) cout << endl; // 空行分隔
        cout << "============== 动态链接器 适配 ==============" << endl;
        cout << "目标 路径: " << target_path << endl;
        if (!interp_path.empty()) cout << "新解释器: " << interp_path << endl;
        if (dry_run) cout << "试运行模式，不修改文件。" << endl;

        OperInterp operInterp;
        bool success = false;
        if (print_interp) {
            success = operInterp.showTargets(target_path);
        } else {
            success = operInterp.adaptTargets(target_path, interp_path, dry_run);
        }
        if (!success) {
            cerr << "动态链接器适配失败，请检查日志。" << endl;
            return 1;
        }

        if (print_interp) cout << "动态链接器查看完成。" << endl;
        else if (dry_run) cout << "动态链接器试运行完成，未修改文件。" << endl;
        else cout << "动态链接器适配完成。" << endl;
    }

    // ================== 内核模块(KO) 适配 ==================
    if (run_kmod) {
        if (run_glibc || run_interp) cout << endl; // 空行分隔
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

        const bool success = modifier->motify(vermagic_str);
        if (success) {
            cout << "内核模块适配成功。" << endl;
        } else {
            cerr << "内核模块适配过程中存在失败项，请检查日志。" << endl;
        }
        delete modifier;
        if (!success) return 1;
    }

    cout << "--------------------------------------------------------" << endl;
    cout << "任务结束。" << endl;

    return 0;
}
