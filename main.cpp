#include <iostream>
#include <cstring>
#include <string>
#include <memory>
#include <getopt.h>
#include "OperGlibc.h"
#include "OperKmod.h"
#include "OperInterp.h"
#include "OperRpath.h"
#include "ElfUtils.h"
#include "help.hpp"

using namespace std;

// 内核模块按目标的 ELF 类别分发到对应的模板实例；定义在 OperKmod.cpp
// 中显式实例化，这里只通过声明调用。
template <typename Traits>
bool runKmod(const std::string& kmodPath, const std::string& symversPath, const std::string& vermagic)
{
    std::unique_ptr<ElfModify<Traits>> modifier;
    if (symversPath.empty()) modifier.reset(new ElfModify<Traits>(kmodPath));
    else modifier.reset(new ElfModify<Traits>(symversPath, kmodPath));
    return modifier->motify(vermagic);
}

int main(int argc, char **argv)
{
    vector<string> libc_paths;
    string target_path;
    string kmod_path;
    string symvers_path;
    string vermagic_str;
    vector<string> interp_paths;
    bool print_interp = false;
    string rpath_value;
    bool set_rpath = false;
    bool print_rpath = false;
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
        {"rpath",    required_argument, 0, 'r'},
        {"print-rpath", no_argument,    0, 1002},
        {"dry-run",  no_argument,       0, 1000},
        {0, 0, 0, 0}
    };

    int opt = 0;
    int option_index = 0;

    // "hc:t:m:s:v:i:pr:" 是短选项字符串，带有冒号的表示需要提供参数
    while ((opt = getopt_long(argc, argv, "hc:t:m:s:v:i:pr:", long_options, &option_index)) != -1)
    {
        switch (opt) {
        case 'h':
            show_help = true;
            break;
        case 'c':
            libc_paths.push_back(optarg);
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
            interp_paths.push_back(optarg);
            break;
        case 'p':
            print_interp = true;
            break;
        case 'r':
            rpath_value = optarg;
            set_rpath = true;
            break;
        case 1002:
            print_rpath = true;
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
    // -c 与 -i 均可重复指定（如同时给 32 位与 64 位的 libc/解释器），
    // 每个目标按类别、字节序、机器架构挑选匹配的候选。
    bool run_glibc = !libc_paths.empty();
    bool run_kmod  = !kmod_path.empty();
    bool run_interp = !interp_paths.empty() || print_interp;
    bool run_rpath = set_rpath || print_rpath;

    if (!show_help && !run_glibc && !run_kmod && !run_interp && !run_rpath) {
        cerr << "错误：必须指定 GLIBC 适配参数(-c/-t)、内核模块适配参数(-m)、解释器适配参数(-i/-p) 或 RPATH 适配参数(-r/--print-rpath)。" << endl;
        show_help = true;
        argument_error = true;
    }

    // 校验 GLIBC 参数
    if (!show_help && run_glibc) {
        if (libc_paths.empty() || target_path.empty()) {
            cerr << "错误：对于GLIBC适配，--libc 和 --target 都是必需参数。" << endl;
            show_help = true;
            argument_error = true;
        }
        for (const auto& libc : libc_paths) {
            if (libc.find("libc.so") == string::npos) {
                cerr << "错误：--libc 参数 \"" << libc << "\" 似乎不是一个有效的 libc.so 文件。" << endl;
                show_help = true;
                argument_error = true;
            }
        }
    }

    // 校验解释器参数
    if (!show_help && run_interp) {
        if (target_path.empty()) {
            cerr << "错误：对于解释器适配，--target 是必需参数。" << endl;
            show_help = true;
            argument_error = true;
        } else if (print_interp && !interp_paths.empty()) {
            cerr << "错误：--print-interp 不能与 --interp 同时使用。" << endl;
            show_help = true;
            argument_error = true;
        } else if (print_interp && dry_run) {
            cerr << "错误：--print-interp 不能与 --dry-run 同时使用。" << endl;
            show_help = true;
            argument_error = true;
        }
        for (const auto& interp : interp_paths) {
            if (!interp.empty() && interp[0] != '/') {
                cerr << "错误：--interp 参数 \"" << interp << "\" 必须是绝对路径。" << endl;
                show_help = true;
                argument_error = true;
            }
        }
    }

    // 校验 RPATH 参数
    if (!show_help && run_rpath) {
        if (target_path.empty()) {
            cerr << "错误：对于 RPATH 适配，--target 是必需参数。" << endl;
            show_help = true;
            argument_error = true;
        } else if (print_rpath && set_rpath) {
            cerr << "错误：--print-rpath 不能与 --rpath 同时使用。" << endl;
            show_help = true;
            argument_error = true;
        } else if (print_rpath && dry_run) {
            cerr << "错误：--print-rpath 不能与 --dry-run 同时使用。" << endl;
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
        for (const auto& libc : libc_paths) cout << "GLIBC 路径: " << libc << endl;
        cout << "目标 路径: " << target_path << endl;

        OperGlibc localVersion;
        if (!localVersion.adaptedTargets(target_path, libc_paths)) {
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
        for (const auto& interp : interp_paths) cout << "新解释器: " << interp << endl;
        if (dry_run) cout << "试运行模式，不修改文件。" << endl;

        OperInterp operInterp;
        bool success = false;
        if (print_interp) {
            success = operInterp.showTargets(target_path);
        } else {
            success = operInterp.adaptTargets(target_path, interp_paths, dry_run);
        }
        if (!success) {
            cerr << "动态链接器适配失败，请检查日志。" << endl;
            return 1;
        }

        if (print_interp) cout << "动态链接器查看完成。" << endl;
        else if (dry_run) cout << "动态链接器试运行完成，未修改文件。" << endl;
        else cout << "动态链接器适配完成。" << endl;
    }

    // ================== RPATH 适配 ==================
    if (run_rpath) {
        if (run_glibc || run_interp) cout << endl; // 空行分隔
        cout << "============== RPATH 适配 ==============" << endl;
        cout << "目标 路径: " << target_path << endl;
        if (set_rpath) cout << "新 RPATH: " << (rpath_value.empty() ? "(清空)" : rpath_value) << endl;
        if (dry_run) cout << "试运行模式，不修改文件。" << endl;

        OperRpath operRpath;
        bool success = false;
        if (print_rpath) {
            success = operRpath.showTargets(target_path);
        } else {
            success = operRpath.adaptTargets(target_path, rpath_value, dry_run);
        }
        if (!success) {
            cerr << "RPATH 适配失败，请检查日志。" << endl;
            return 1;
        }

        if (print_rpath) cout << "RPATH 查看完成。" << endl;
        else if (dry_run) cout << "RPATH 试运行完成，未修改文件。" << endl;
        else cout << "RPATH 适配完成。" << endl;
    }

    // ================== 内核模块(KO) 适配 ==================
    if (run_kmod) {
        if (run_glibc || run_interp || run_rpath) cout << endl; // 空行分隔
        cout << "============== 内核模块 适配 ==============" << endl;
        cout << "目标模块: " << kmod_path << endl;
        if (!symvers_path.empty()) cout << "符号文件(Symvers): " << symvers_path << endl;
        if (!vermagic_str.empty()) cout << "目标版本魔术(Vermagic): " << vermagic_str << endl;

        // 根据是否提供了 Module.symvers 文件选择不同的构造函数。
        // 目标模块按 ELF 类别分发；大端模块直接警告并拒绝。
        ElfIdentity kmodId;
        bool success = false;
        if (!ElfUtils::identifyElf(kmod_path, kmodId)) {
            cerr << "错误：目标模块不是有效的 ELF 文件。" << endl;
            return 1;
        }
        if (kmodId.elfData == ELFDATA2MSB) {
            ElfUtils::warnUnsupportedEndian(kmod_path, kmodId);
            return 1;
        }
        if (kmodId.elfClass == ELFCLASS64)
            success = runKmod<Elf64Traits>(kmod_path, symvers_path, vermagic_str);
        else
            success = runKmod<Elf32Traits>(kmod_path, symvers_path, vermagic_str);
        if (success) {
            cout << "内核模块适配成功。" << endl;
        } else {
            cerr << "内核模块适配过程中存在失败项，请检查日志。" << endl;
        }
        if (!success) return 1;
    }

    cout << "--------------------------------------------------------" << endl;
    cout << "任务结束。" << endl;

    return 0;
}
