#include "GlibcOper.h"
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

GlibcOper::GlibcOper()
{

}

GlibcOper::~GlibcOper()
{
    if(glibcPtrs)
    {
        delete glibcPtrs;
        glibcPtrs = NULL;
    }
    if(targetElfPtrs)
    {
        delete targetElfPtrs;
        targetElfPtrs = NULL;
    }
}

void GlibcOper::initGlibcInfo(string glibcPath)
{
    if(glibcPtrs) return;
    glibcPtrs = new ElfPtrs(0);
    if(!glibcPtrs)
    {
        ErrorLog::getErrorLog()->putErrInfo("初始化GlibcElf失败",glibcPath);
        return;
    }
    if(!glibcPtrs->initPtrs(glibcPath.c_str()))
    {
        ErrorLog::getErrorLog()->putErrInfo("initGlibcPtrs失败",glibcPath);
        return;
    }

    Elf64_Verdef *verdef = glibcPtrs->verdef;
    Elf64_Verdef *next_verdef;
    vector<string> dynsymStrs_vct;
    // 遍历 .gnu.version_d 表d define
    for (int last = 0; !last; verdef = next_verdef)
    {
        if(verdef->vd_flags == 0)
        {
            Elf64_Verdaux *daux = (typeof(daux))((char *)verdef + verdef->vd_aux);
            Elf64_Verdaux *next_daux;
            // 获取 Elf64_Verdaux 数组的元素数，首个元素
            // 遍历 Elf64_Verdaux 数组（记录每一个依赖版本的信息）
            for (int cnt = verdef->vd_aux; cnt--; daux = next_daux)
            {
                char *name = glibcPtrs->dynstr + daux->vda_name; // GLIBC_xxx 字符串
                bool add = true;
                for(int j = 0; j < glibcVersionInfo_vct.size(); ++j)
                    if(strcmp(glibcVersionInfo_vct[j].first.first.c_str(),name) == 0)
                        add = false;
                if(add)
                {
                    dynsymStrs_vct.clear();
                    for (int i = 1; i < glibcPtrs->sh_version->sh_size / sizeof(unsigned short); i++)
                    {
                        string dynsym = string(glibcPtrs->dynstr + glibcPtrs->dynsym[i].st_name);
                        if(glibcPtrs->versions[i] == verdef->vd_ndx && dynsym != name)
                            dynsymStrs_vct.push_back(string(glibcPtrs->dynstr + glibcPtrs->dynsym[i].st_name));
                    }
                    glibcVersionInfo_vct.push_back(pair<pair<string, int>, vector<string> >(pair<string, int>(name,verdef->vd_ndx),dynsymStrs_vct));
                }
                // 指向下一个元素
                next_daux = (typeof(next_daux))((char *)daux + daux->vda_next);
            }
        }
        // 获取下一个 表项
        next_verdef = (typeof(next_verdef))((char *)verdef + verdef->vd_next);
        // 判断当前是否是最后一个表项了
        last = verdef->vd_next == 0;
    }
    showGlibcInfo();
}

void GlibcOper::showGlibcInfo(bool showDynsym)
{
    if(showDynsym)
        cout<<"\033[1m目标GLIBC所支持的版本及对应符号表如下:\033[0m"<<endl;
    else
        cout<<"\033[1m目标GLIBC所支持的版本如下:\033[0m"<<endl;
    for(auto ver : glibcVersionInfo_vct)
    {
        cout<<"\033[31;1m"<<ver.first.first<<" "<<ver.first.second<<"\033[0m"<<endl;
        if(showDynsym)
        {
            int i = 0;
            for(auto dynsym : ver.second)
            {
                cout<<dynsym<<" ";
                if(i % 10 == 0 && i != 0)
                    cout<<endl;
                ++i;
            }
            cout<<endl;
        }
    }
}

void GlibcOper::adaptedTargets(string path)
{
    DIR *dir = opendir(path.c_str());
    targetMaxPathLen = path.size() + 4;
    if(dir)
    {
        closedir(dir);
        getAllElf(path.c_str());
        for(string str : targetElf_vct)
            if(str.size() > targetMaxPathLen) targetMaxPathLen = str.size() +4;
        for(string str : targetElf_vct)
            adaptedTargetElfFileGlibcVersion(str);
    }
    else
        adaptedTargetElfFileGlibcVersion(path);
    return;
}

void GlibcOper::clearContainer()
{
    if(targetElfPtrs)
    {
        delete targetElfPtrs;
        targetElfPtrs = NULL;
    }
    targetVersionInfo_vct.clear();
    validIndexAndId_vct.clear();
    return;
}

void GlibcOper::getAllElf(const char *dir)
{
    DIR *d; //声明一个句柄
    struct dirent *file; //readdir函数的返回值就存放在这个结构体中
    struct stat sb;
    string filePath;

    if(!(d = opendir(dir))) return;
    while((file = readdir(d)) != NULL)
    {
        //把当前目录.，上一级目录都去掉，避免死循环遍历目录
        if(strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0)
            continue;
        filePath = string(dir) + "/" + file->d_name;
        if(stat(filePath.c_str(), &sb) >= 0)
        {
            //判断该文件是否是目录且非链接文件
            if(S_ISDIR(sb.st_mode) && !S_ISLNK(sb.st_mode))
                getAllElf(filePath.c_str());
            else if(S_ISREG(sb.st_mode))
            {
                //只是添加Elf文件
                if(isElf(filePath.c_str()))
                    targetElf_vct.push_back(filePath);
            }
        }
    }
    closedir(d);
    return;
}

bool GlibcOper::isElf(const char *file)
{
    bool ret = false;
    int fd = 0;
    char elfHead[7] = {0x7f,0x45,0x4c,0x46,0x02,0x01,0x01};
    char targetHead[7] = { 0 };
    fd = open(file,O_RDONLY);
    if(fd < 0) return false;
    if(sizeof(targetHead) == read(fd,targetHead,sizeof(targetHead)))
        if(!memcmp(elfHead,targetHead,sizeof(targetHead))) ret = true;
    close(fd);
    return ret;
}

bool GlibcOper::adaptedTargetElfFileGlibcVersion(string path)
{
    clearContainer();
    if(!glibcVersionInfo_vct.size()) return false;
    targetElfPtrs = new ElfPtrs(1);
    if(!targetElfPtrs)
    {
        ErrorLog::getErrorLog()->putErrInfo("初始化TargetElf失败",path);
        return false;
    }
    if(!targetElfPtrs->initPtrs(path.c_str()))
    {
        ErrorLog::getErrorLog()->putErrInfo("initTargetPtrs失败",path);
        return false;
    }

    if(!checkFountDynsym()) return false;
    /**
    * typedef struct {
    *         Elf64_Half      vn_version;
    *         // 此成员标识该结构的版本(0表示无效版本)
    *         Elf64_Half      vn_cnt;
    *          // Elf64_Vernaux 数组中的元素数目
    *         Elf64_Word      vn_file;
    *         // 以空字符结尾的字符串的字符串表偏移，用于提供版本依赖性的文件名。此名称与文件中找到的 .dynamic 依赖项之一匹配。
    *         Elf64_Word      vn_aux;
    *         // 字节偏移，范围从此 Elf64_Verneed 项的开头到关联文件依赖项所需的版本定义的 Elf64_Vernaux数组。必须存在至少一种版本依赖性。也可以存在其他版本依赖性，具体数目由 vn_cnt 值表示。
    *         Elf64_Word      vn_next;
    *         // 从此 Elf64_Verneed 项的开头到下一个 Elf64_Verneed 项的字节偏移
    * } Elf64_Verneed;
    */
    // 获取下一个 表项
    Elf64_Verneed *next_verneed = (Elf64_Verneed *)((char *)targetElfLibcVerneed + targetElfLibcVerneed->vn_next);
    // 获取 Elf64_Vernaux 数组的结尾
    char *end_of_naux = (char *)next_verneed;
    // 判断当前是否是最后一个表项了
    if (targetElfLibcVerneed->vn_next == 0)
        end_of_naux = (char *)targetElfPtrs->elf_hdr + targetElfPtrs->sh_version_r->sh_offset + targetElfPtrs->sh_version_r->sh_size;

    /**
    * typedef struct {
    *         Elf64_Word       vna_hash;     // 版本依赖性名称的散列值
    *         Elf64_Half         vna_flags;    // 版本依赖性特定信息(VER_FLG_WEAK[0x2]弱版本标识符)
    *         Elf64_Half        vna_other;    // 目前未使用
    *         Elf64_Word      vna_name;    // 以空字符结尾的字符串的字符串表偏移，用于提供版本依赖性的名称。
    *         Elf64_Word        vna_next;    // 从此 Elf64_Vernaux 项的开头到下一个 Elf64_Vernaux 项的字节偏移
    * } Elf64_Vernaux;
    */
    // 获取 Elf64_Vernaux 数组的元素数，首个元素
    Elf64_Vernaux *naux = (Elf64_Vernaux *)((char *)targetElfLibcVerneed + targetElfLibcVerneed->vn_aux);
    Elf64_Vernaux *next_naux;
    // 遍历 Elf64_Vernaux 数组（记录每一个依赖版本的信息）
    for (unsigned cnt = targetElfLibcVerneed->vn_cnt; cnt--; naux = next_naux)
    {
        char *name = targetElfPtrs->dynstr + naux->vna_name; // GLIBC_xxx 字符串
        // 指向下一个元素
        next_naux = (typeof(next_naux))((char *)naux + naux->vna_next);
        //如果本地glibc库没有包含elf文件所需要的版本
        if(containsVersion(name).second == -1)
        {
            // 将整个 Elf64_Vernaux 数组当前项后面元素向前移动
            // 也就是将当前项从数组中移除掉
            if (cnt > 0 /*剩余未处理元素必须大于0，才需要*/ )
            {
                memmove(naux, next_naux, end_of_naux - (char *)next_naux);
            }
            // 下一个指向当前，也就是前移一个元素（因为这个元素已经被覆盖了，或者就是最后一个）
            next_naux = naux;
        }
    }

    for(int i = 0;i<validIndexAndId_vct.size();++i)
    {
        if(targetVersionInfo_vct[validIndexAndId_vct[i].first.second].second != validIndexAndId_vct[i].second.second)
        {
            const char* dynsymName = targetElfPtrs->dynstr + targetElfPtrs->dynsym[validIndexAndId_vct[i].first.first].st_name;
            // 打印修改过程
            printf("  修改 %-20s: %-12s(%02u) ----> %-12s(%02u)  文件:%-20s\n",dynsymName,targetVersionInfo_vct[validIndexAndId_vct[i].first.second].first.c_str(),
                    (unsigned short)targetVersionInfo_vct[validIndexAndId_vct[i].first.second].second,validIndexAndId_vct[i].second.first.c_str(),
                    (unsigned short)validIndexAndId_vct[i].second.second,path.c_str());
            // 修改与本地对应版本
            targetElfPtrs->versions[validIndexAndId_vct[i].first.first] = validIndexAndId_vct[i].second.second; // 这里也可以写成 =0 (local defualt)
        }
    }
    return true;
}

bool GlibcOper::checkFountDynsym()
{
    bool ret = true;
    targetElfLibcVerneed = NULL;
    Elf64_Verneed *verneed = targetElfPtrs->verneed;
    while(true)
    {
        if(strcmp(targetElfPtrs->dynstr + verneed->vn_file, "libc.so.6"))
        {
            if(verneed->vn_next == 0) break;
            verneed = (typeof(verneed))((char *)verneed + verneed->vn_next);
            continue;
        }
        targetElfLibcVerneed = verneed;
        Elf64_Vernaux *naux = (typeof(naux))((char *)verneed + verneed->vn_aux);
        while(true)
        {
            char *name = targetElfPtrs->dynstr + naux->vna_name;
            targetVersionInfo_vct.push_back(pair<string,int>(name,naux->vna_other));
            //printf("name = %s  naux->vna_other = %d\n",name,naux->vna_other);
            if(naux->vna_next == 0) break;
            naux = (typeof(naux))((char *)naux + naux->vna_next);
        }
        break;
    }
    if(!targetElfLibcVerneed)
    {
        ErrorLog::getErrorLog()->putErrInfo("未能找到libc.so.6所在节表", targetElfPtrs->getFilePath());
        return false;
    }

    for (int i = 1; i < targetElfPtrs->sh_version->sh_size / sizeof(unsigned short); i++)
    {
        unsigned short v = targetElfPtrs->versions[i];
        unsigned hveridx = 0;
        for (; hveridx < targetVersionInfo_vct.size(); ++hveridx)
            if (v == targetVersionInfo_vct[hveridx].second) break;
        if (hveridx == targetVersionInfo_vct.size()) continue;

        const char* dynsymName = targetElfPtrs->dynstr + targetElfPtrs->dynsym[i].st_name;
        pair<string, int> verAndId = containsDynsym(string(dynsymName));
        if(verAndId.second == -1)
        {
            char errInfo[2048] = { 0 };
            sprintf(errInfo,"目标C库中未找到符号:%-20s    GLIBC版本:%-10s",dynsymName,targetVersionInfo_vct[hveridx].first.c_str());
            ErrorLog::getErrorLog()->putErrInfo(errInfo, targetElfPtrs->getFilePath());
            ret = false;
        }
        else
        {
            //查找目标elf中对应版本的id
            for(auto vInfo : targetVersionInfo_vct)
                if(vInfo.first == verAndId.first)
                {
                    verAndId.second = vInfo.second;
                    break;
                }
            if(verAndId.second != -1)
                validIndexAndId_vct.push_back(pair<pair<int,int>,pair<string,int>>(pair<int,int>(i,hveridx),verAndId));
            else
            {
                ErrorLog::getErrorLog()->putErrInfo("未能找到目标elf中对应版本的id",targetElfPtrs->getFilePath());
                ret = false;
            }
        }
    }
    return ret;
}

pair<string, int> GlibcOper::containsVersion(string version)
{
    for(auto ver : glibcVersionInfo_vct)
    {
        if(ver.first.first == version)
            return ver.first;
    }
    return pair<string, int>("",-1);
}

pair<string, int> GlibcOper::containsDynsym(string dynsym)
{
    for(auto ver : glibcVersionInfo_vct)
    {
        for(auto str : ver.second)
        {
            if(str == dynsym)
                return ver.first;
        }
    }
    return pair<string, int>("",-1);
}
