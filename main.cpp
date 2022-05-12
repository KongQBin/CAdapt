#include <iostream>
#include <cstring>
#include "GlibcOper.h"
using namespace std;
int main(int argc,char **argv)
{
    bool help = false;
    if(!argc || argc != 3)
        help = true;
    else if(!strstr(argv[1],"libc.so"))
        help = true;

    if(help)
    {
        cout<<"------------------------->Help Info<-------------------------"<<endl;
        cout<<"argv[1] = 提供符号表的GLIBC(libc.so.6)文件位置"<<endl;
        cout<<"argv[2] = 要适配GLIBC版本及符号表的ELF文件/所在目录"<<endl;
        return 0;
    }

    GlibcOper localVersion;
    localVersion.initGlibcInfo(string(argv[1]));
    localVersion.adaptedTargets(string(argv[2]));

    return 0;
}
