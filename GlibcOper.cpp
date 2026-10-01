#include "GlibcOper.h"
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <set>
#include <utility>

using namespace std;

void GlibcOper::initGlibcInfo(const string& glibcPath)
{
    if(glibcPtrs) return;
    glibcPtrs.reset(new ElfPtrs(ElfOpenMode::ReadOnly));
    if(!glibcPtrs)
    {
        ErrorLog::getErrorLog()->putErrInfo("初始化GlibcElf失败(new)", glibcPath);
        return;
    }
    if(!glibcPtrs->initPtrs(glibcPath.c_str()))
    {
        ErrorLog::getErrorLog()->putErrInfo("initGlibcPtrs失败", glibcPath);
        return;
    }

    // 获取必需的表信息
    TableInfo *tb_version_d = glibcPtrs->getTableInfo(".gnu.version_d");
    TableInfo *tb_dynstr = glibcPtrs->getTableInfo(".dynstr");
    TableInfo *tb_dynsym = glibcPtrs->getTableInfo(".dynsym");
    TableInfo *tb_version = glibcPtrs->getTableInfo(".gnu.version");

    if(!tb_version_d || !tb_dynstr || !tb_dynsym || !tb_version)
    {
        ErrorLog::getErrorLog()->putErrInfo("获取Glibc必要节区失败", glibcPath);
        return;
    }

    // 计算实际偏移
    char* ehdr = reinterpret_cast<char*>(glibcPtrs->elf_hdr);
    Elf64_Verdef *verdef = reinterpret_cast<Elf64_Verdef*>(ehdr + tb_version_d->addr->sh_offset);
    char *dynstr = ehdr + tb_dynstr->addr->sh_offset;
    Elf64_Sym *dynsym = reinterpret_cast<Elf64_Sym*>(ehdr + tb_dynsym->addr->sh_offset);
    unsigned short *versions = reinterpret_cast<unsigned short*>(ehdr + tb_version->addr->sh_offset);

    Elf64_Verdef *next_verdef;
    set<string> seenVersions;

    for (int last = 0; !last; verdef = next_verdef)
    {
        if(verdef->vd_flags == 0)
        {
            Elf64_Verdaux *daux = reinterpret_cast<Elf64_Verdaux*>((char *)verdef + verdef->vd_aux);
            Elf64_Verdaux *next_daux;

            for (int cnt = verdef->vd_aux; cnt--; daux = next_daux)
            {
                char *name = dynstr + daux->vda_name;

                if(seenVersions.find(name) == seenVersions.end())
                {
                    seenVersions.insert(name);
                    GlibcVersionInfo versionInfo;
                    versionInfo.name = name;
                    versionInfo.id = verdef->vd_ndx;

                    for (size_t i = 1; i < tb_version->addr->sh_size / sizeof(unsigned short); i++)
                    {
                        if(versions[i] == verdef->vd_ndx)
                        {
                            string s_dynsym = string(dynstr + dynsym[i].st_name);
                            if(s_dynsym != name)
                                versionInfo.symbols.push_back(move(s_dynsym));
                        }
                    }
                    glibcVersionInfo_vct.push_back(move(versionInfo));
                }
                next_daux = reinterpret_cast<Elf64_Verdaux*>((char *)daux + daux->vda_next);
            }
        }
        next_verdef = reinterpret_cast<Elf64_Verdef*>((char *)verdef + verdef->vd_next);
        last = verdef->vd_next == 0;
    }
    showGlibcInfo();
}

void GlibcOper::showGlibcInfo(bool showDynsym) const
{
    const char* reset = "\033[0m";
    const char* bold = "\033[1m";
    const char* redBold = "\033[31;1m";

    if(showDynsym)
        cout << bold << "目标GLIBC所支持的版本及对应符号表如下:" << reset << endl;
    else
        cout << bold << "目标GLIBC所支持的版本如下:" << reset << endl;

    for (const auto& ver : glibcVersionInfo_vct)
    {
        cout << redBold << ver.name << " " << ver.id << reset << endl;
        if(showDynsym)
        {
            int i = 0;
            for(const auto& dynsym : ver.symbols)
            {
                cout << dynsym << " ";
                if(i % 10 == 0 && i != 0)
                    cout << endl;
                ++i;
            }
            cout << endl;
        }
    }
}

void GlibcOper::adaptedTargets(const string& path)
{
    DIR *dir = opendir(path.c_str());
    targetMaxPathLen = path.size() + 4;
    if(dir)
    {
        closedir(dir);
        getAllElf(path.c_str());

        for(const auto& str : targetElf_vct)
        {
            if(str.size() > (size_t)targetMaxPathLen) targetMaxPathLen = str.size() + 4;
        }

        for(const auto& str : targetElf_vct)
        {
            adaptedTargetElfFileGlibcVersion(str);
        }
    }
    else
        adaptedTargetElfFileGlibcVersion(path);
    return;
}

void GlibcOper::clearContainer()
{
    targetElfPtrs.reset();
    targetVersionInfo_vct.clear();
    validIndexAndId_vct.clear();
}

void GlibcOper::getAllElf(const char *dir)
{
    DIR *d;
    struct dirent *file;
    struct stat sb;
    string filePath;

    if(!(d = opendir(dir))) return;
    while((file = readdir(d)) != NULL)
    {
        if(strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0)
            continue;
        filePath = string(dir) + "/" + file->d_name;
        if(stat(filePath.c_str(), &sb) >= 0)
        {
            if(S_ISDIR(sb.st_mode) && !S_ISLNK(sb.st_mode))
                getAllElf(filePath.c_str());
            else if(S_ISREG(sb.st_mode))
            {
                if(isElf(filePath.c_str()))
                    targetElf_vct.push_back(filePath);
            }
        }
    }
    closedir(d);
    return;
}

bool GlibcOper::isElf(const char *file) const
{
    bool ret = false;
    const char elfHead[7] = {0x7f,0x45,0x4c,0x46,0x02,0x01,0x01};
    char targetHead[7] = { 0 };
    int fd = 0;

    fd = open(file, O_RDONLY);
    if(fd < 0) return false;

    if(sizeof(targetHead) == read(fd,targetHead,sizeof(targetHead)))
        if(memcmp(elfHead,targetHead,sizeof(targetHead)) == 0) ret = true;
    close(fd);
    return ret;
}

bool GlibcOper::adaptedTargetElfFileGlibcVersion(const string& path)
{
    clearContainer();
    if(glibcVersionInfo_vct.empty()) return false;

    targetElfPtrs.reset(new ElfPtrs(ElfOpenMode::ReadWrite));
    if(!targetElfPtrs)
    {
        ErrorLog::getErrorLog()->putErrInfo("初始化TargetElf失败(new)", path);
        return false;
    }
    if(!targetElfPtrs->initPtrs(path.c_str()))
    {
        ErrorLog::getErrorLog()->putErrInfo("initTargetPtrs失败", path);
        return false;
    }

    if(!checkFoundDynsym()) return false;

    unsigned removed_count = 0;
    unsigned original_cnt = 0;

    if (targetElfLibcVerneed)
    {
        original_cnt = targetElfLibcVerneed->vn_cnt;
    }

    TableInfo* tb_dynstr = targetElfPtrs->getTableInfo(".dynstr");
    char* target_dynstr = tb_dynstr ? (reinterpret_cast<char*>(targetElfPtrs->elf_hdr) + tb_dynstr->addr->sh_offset) : nullptr;

    if(!target_dynstr) return false;

    if (targetElfLibcVerneed && original_cnt > 0 && targetElfLibcVerneed->vn_aux != 0)
    {
        Elf64_Vernaux *naux = reinterpret_cast<Elf64_Vernaux *>((char *)targetElfLibcVerneed + targetElfLibcVerneed->vn_aux);
        Elf64_Vernaux *prev_naux = nullptr;

        for (unsigned cnt = 0; cnt < original_cnt; ++cnt)
        {
            char *name = target_dynstr + naux->vna_name;

            Elf64_Vernaux *next_naux = nullptr;
            if (naux->vna_next != 0)
                next_naux = reinterpret_cast<Elf64_Vernaux*>((char *)naux + naux->vna_next);

            if(containsVersion(name).second == -1)
            {
                removed_count++;
                if (prev_naux)
                {
                    prev_naux->vna_next = naux->vna_next;
                }
                else
                {
                    if (next_naux)
                    {
                        targetElfLibcVerneed->vn_aux = (char*)next_naux - (char*)targetElfLibcVerneed;
                    }
                    else
                    {
                        targetElfLibcVerneed->vn_aux = 0;
                    }
                }
            }
            else
            {
                prev_naux = naux;
            }

            naux = next_naux;
            if (naux == nullptr) break;
        }
    }
    else if (targetElfLibcVerneed)
    {
        targetElfLibcVerneed->vn_cnt = 0;
    }

    if (targetElfLibcVerneed && removed_count > 0)
    {
        targetElfLibcVerneed->vn_cnt -= removed_count;
    }

    TableInfo* tb_version = targetElfPtrs->getTableInfo(".gnu.version");
    TableInfo* tb_dynsym = targetElfPtrs->getTableInfo(".dynsym");

    char* target_ehdr = reinterpret_cast<char*>(targetElfPtrs->elf_hdr);
    unsigned short *target_versions = reinterpret_cast<unsigned short*>(target_ehdr + tb_version->addr->sh_offset);
    Elf64_Sym *target_dynsym = reinterpret_cast<Elf64_Sym*>(target_ehdr + tb_dynsym->addr->sh_offset);

    for(const auto& patch : validIndexAndId_vct)
    {
        if(targetVersionInfo_vct[patch.originalTargetVersionIndex].id != patch.hostVersionId)
        {
            const char* dynsymName = target_dynstr + target_dynsym[patch.symbolIndex].st_name;

            printf("  修改 %-20s: %-12s(%02u) ----> %-12s(%02u)  文件:%-20s\n",
                   dynsymName,
                   targetVersionInfo_vct[patch.originalTargetVersionIndex].name.c_str(),
                   (unsigned short)targetVersionInfo_vct[patch.originalTargetVersionIndex].id,
                   patch.hostVersionName.c_str(),
                   (unsigned short)patch.hostVersionId,
                   path.c_str());

            target_versions[patch.symbolIndex] = patch.hostVersionId;
        }
    }
    return true;
}

bool GlibcOper::checkFoundDynsym()
{
    bool ret = true;
    targetElfLibcVerneed = nullptr;

    TableInfo* tb_version_r = targetElfPtrs->getTableInfo(".gnu.version_r");
    TableInfo* tb_dynstr = targetElfPtrs->getTableInfo(".dynstr");
    TableInfo* tb_version = targetElfPtrs->getTableInfo(".gnu.version");
    TableInfo* tb_dynsym = targetElfPtrs->getTableInfo(".dynsym");

    if(!tb_version_r || !tb_dynstr || !tb_version || !tb_dynsym)
    {
        ErrorLog::getErrorLog()->putErrInfo("目标ELF缺少必要节区", targetElfPtrs->getFilePath());
        return false;
    }

    char* ehdr = reinterpret_cast<char*>(targetElfPtrs->elf_hdr);
    Elf64_Verneed *verneed = reinterpret_cast<Elf64_Verneed*>(ehdr + tb_version_r->addr->sh_offset);
    char *dynstr = ehdr + tb_dynstr->addr->sh_offset;
    unsigned short *versions = reinterpret_cast<unsigned short*>(ehdr + tb_version->addr->sh_offset);
    Elf64_Sym *dynsym = reinterpret_cast<Elf64_Sym*>(ehdr + tb_dynsym->addr->sh_offset);

    while(true)
    {
        if(strcmp(dynstr + verneed->vn_file, "libc.so.6"))
        {
            if(verneed->vn_next == 0) break;
            verneed = reinterpret_cast<Elf64_Verneed*>((char *)verneed + verneed->vn_next);
            continue;
        }

        targetElfLibcVerneed = verneed;

        if (verneed->vn_cnt > 0 && verneed->vn_aux != 0)
        {
            Elf64_Vernaux *naux = reinterpret_cast<Elf64_Vernaux*>((char *)verneed + verneed->vn_aux);
            while(true)
            {
                char *name = dynstr + naux->vna_name;
                targetVersionInfo_vct.push_back({string(name), naux->vna_other});

                if(naux->vna_next == 0) break;
                naux = reinterpret_cast<Elf64_Vernaux*>((char *)naux + naux->vna_next);
            }
        }
        break;
    }
    if(!targetElfLibcVerneed)
    {
        ErrorLog::getErrorLog()->putErrInfo("未能找到libc.so.6所在节表", targetElfPtrs->getFilePath());
        return false;
    }

    for (size_t i = 1; i < tb_version->addr->sh_size / sizeof(unsigned short); i++)
    {
        unsigned short v = versions[i];
        unsigned hveridx = 0;
        for (; hveridx < targetVersionInfo_vct.size(); ++hveridx)
            if (v == targetVersionInfo_vct[hveridx].id) break;
        if (hveridx == targetVersionInfo_vct.size()) continue;

        const char* dynsymName = dynstr + dynsym[i].st_name;
        auto verAndId = containsDynsym(string(dynsymName));
        if(verAndId.second == -1)
        {
            char errInfo[2048] = { 0 };
            sprintf(errInfo,"目标C库中未找到符号:%-20s    GLIBC版本:%-10s", dynsymName, targetVersionInfo_vct[hveridx].name.c_str());
            ErrorLog::getErrorLog()->putErrInfo(errInfo, targetElfPtrs->getFilePath());
            ret = false;
        }
        else
        {
            int hostIdInTarget = -1;
            for(const auto& vInfo : targetVersionInfo_vct)
            {
                if(vInfo.name == verAndId.first)
                {
                    hostIdInTarget = vInfo.id;
                    break;
                }
            }

            if(hostIdInTarget != -1)
            {
                validIndexAndId_vct.push_back({(int)i, (int)hveridx, verAndId.first, hostIdInTarget});
            }
            else
            {
                ErrorLog::getErrorLog()->putErrInfo("未能找到目标elf中对应版本的id", targetElfPtrs->getFilePath());
                ret = false;
            }
        }
    }
    return true;
}

pair<string, int> GlibcOper::containsVersion(const string& version) const
{
    for (const auto& ver : glibcVersionInfo_vct)
    {
        if(ver.name == version)
            return {ver.name, ver.id};
    }
    return {"", -1};
}

pair<string, int> GlibcOper::containsDynsym(const string& dynsym) const
{
    for (const auto& ver : glibcVersionInfo_vct)
    {
        for (const auto& str : ver.symbols)
        {
            if(str == dynsym)
                return {ver.name, ver.id};
        }
    }
    return {"", -1};
}
