#include <iostream>
#include <cstring>
#include <string>     // 包含 std::string
#include <getopt.h>   // 包含 getopt_long
#include "GlibcOper.h"

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
    cout << "Options:" << endl;
    cout << "  -c, --libc <path>     必需。指定提供符号表的GLIBC(libc.so.6)文件位置。" << endl;
    cout << "  -t, --target <path>   必需。指定要适配GLIBC版本及符号表的ELF文件/所在目录。" << endl;
    cout << "  -h, --help            显示此帮助信息。" << endl;
    cout << endl;
    cout << "Example:" << endl;
    cout << "  " << prog_name << " --libc /lib64/libc.so.6 --target ./my_program" << endl;
}

int main(int argc, char **argv)
{
    string libc_path;
    string target_path;
    bool show_help = false;

    // 1. 定义长选项
    static struct option long_options[] = {
        // name,       has_arg,           flag, val
        {"help",    no_argument,       0, 'h'},
        {"libc",    required_argument, 0, 'c'},
        {"target",  required_argument, 0, 't'},
        {0, 0, 0, 0} // 数组结束的哨兵
    };

    int opt = 0;
    int option_index = 0;

    // 2. 循环解析选项
    // "hl:t:" 是短选项字符串：
    // h - 无参数
    // l: - 有必需参数
    // t: - 有必需参数
    while ((opt = getopt_long(argc, argv, "hl:t:", long_options, &option_index)) != -1)
    {
        switch (opt) {
            case 'h':
                show_help = true;
                break;
            case 'c':
                libc_path = optarg; // optarg 是 getopt.h 提供的全局变量，存储选项的参数
                break;
            case 't':
                target_path = optarg;
                break;
            default: // '?' 表示无法识别的选项或缺少参数
                show_help = true;
                break;
        }
    }

    // 3. 检查解析后的参数
    
    // 如果optind（下一个要处理的argv索引）小于argc，说明有多余的非选项参数
    if (optind < argc) {
        cerr << "错误：检测到未知的非选项参数。" << endl;
        show_help = true;
    }

    // 如果没有提供任何参数，也显示帮助
    if (argc == 1) {
        show_help = true;
    }

    // 检查必需参数是否已提供
    if (!show_help && (libc_path.empty() || target_path.empty())) {
        cerr << "错误：--libc 和 --target 都是必需参数。" << endl;
        show_help = true;
    }

    // 保留您原来的检查：确保libc参数看起来正确
    if (!show_help && strstr(libc_path.c_str(), "libc.so") == NULL) {
        cerr << "错误：--libc 参数 \"" << libc_path << "\" 似乎不是一个有效的 libc.so 文件。" << endl;
        show_help = true;
    }

    // 4. 执行或显示帮助
    if (show_help)
    {
        print_help(argv[0]);
        return (argc == 1) ? 0 : 1; // 成功退出或错误退出
    }

    // 5. 执行主逻辑
    cout << "GLIBC 路径: " << libc_path << endl;
    cout << "目标 路径: " << target_path << endl;
    cout << "--------------------------------------------------------" << endl;

    GlibcOper localVersion;
    localVersion.initGlibcInfo(libc_path);
    localVersion.adaptedTargets(target_path);

    cout << "--------------------------------------------------------" << endl;
    cout << "适配完成。" << endl;
    
    return 0;
}
