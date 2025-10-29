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

    Elf64_Verdef *verdef = glibcPtrs->verdef;
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
                char *name = glibcPtrs->dynstr + daux->vda_name;

                if(seenVersions.find(name) == seenVersions.end())
                {
                    seenVersions.insert(name);
                    GlibcVersionInfo versionInfo;
                    versionInfo.name = name;
                    versionInfo.id = verdef->vd_ndx;

                    for (int i = 1; i < glibcPtrs->sh_version->sh_size / sizeof(unsigned short); i++)
                    {
                        if(glibcPtrs->versions[i] == verdef->vd_ndx)
                        {
                            string dynsym = string(glibcPtrs->dynstr + glibcPtrs->dynsym[i].st_name);
                            if(dynsym != name)
                                versionInfo.symbols.push_back(move(dynsym));
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
            if(str.size() > targetMaxPathLen) targetMaxPathLen = str.size() + 4;
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

    // 不再使用 memmove，因为它会破坏文件偏移和节区大小。
    // 相反，通过修改 Elf64_Vernaux 链表指针 (vna_next)
    // 和 Elf64_Verneed 的起始指针 (vn_aux) 来 "跳过" (Bypass)
    // 不需要的数据块。数据块本身保留在文件中，但动态链接器
    // 将不再访问它们，这样文件结构保持一致。

    unsigned removed_count = 0;
    unsigned original_cnt = 0;

    // 确保 targetElfLibcVerneed 是有效的
    if (targetElfLibcVerneed)
    {
        original_cnt = targetElfLibcVerneed->vn_cnt; // 保存原始计数值
    }

    // 检查 targetElfLibcVerneed 是否有效，以及它是否有条目
    if (targetElfLibcVerneed && original_cnt > 0 && targetElfLibcVerneed->vn_aux != 0)
    {
        Elf64_Vernaux *naux = reinterpret_cast<Elf64_Vernaux *>((char *)targetElfLibcVerneed + targetElfLibcVerneed->vn_aux);
        Elf64_Vernaux *prev_naux = nullptr; // 跟踪前一个保留的条目

        // 遍历所有原始条目
        for (unsigned cnt = 0; cnt < original_cnt; ++cnt)
        {
            char *name = targetElfPtrs->dynstr + naux->vna_name;

            // 必须在修改 *之前* 获取下一个条目的指针
            Elf64_Vernaux *next_naux = nullptr;
            if (naux->vna_next != 0)
                next_naux = reinterpret_cast<Elf64_Vernaux*>((char *)naux + naux->vna_next);

            if(containsVersion(name).second == -1) // Glibc库没有这个版本 -> 移除
            {
                removed_count++;
                if (prev_naux)
                {
                    // [情况A: 移除中间或末尾的条目]
                    // 将前一个条目的 'next' 指针指向当前条目的 'next' 指针，
                    // 从而在链表中 "跳过" 当前条目。
                    prev_naux->vna_next = naux->vna_next;
                }
                else
                {
                    // [情况B: 移除第一个条目]
                    // 我们必须更新 Elf64_Verneed 结构中的 vn_aux (起始偏移)
                    // 使其指向 *下一个* 条目。
                    if (next_naux)
                    {
                        // 新的起始偏移是 'next_naux' 相对于 'targetElfLibcVerneed' 的偏移
                        targetElfLibcVerneed->vn_aux = (char*)next_naux - (char*)targetElfLibcVerneed;
                    }
                    else
                    {
                        // 我们移除了唯一的条目。起始偏移置为0。
                        targetElfLibcVerneed->vn_aux = 0;
                    }
                }
                // 'prev_naux' 保持不变，因为它仍然是上一个 *被保留* 的条目。
            }
            else // Glibc库有这个版本 -> 保留
            {
                // [情况C: 保留条目]
                // 此条目被保留，它成为下一次迭代的 "前一个条目"。
                prev_naux = naux;
            }

            naux = next_naux; // 总是移动到下一个条目
            if (naux == nullptr) break; // 已到达链表末尾
        }
    }
    else if (targetElfLibcVerneed)
    {
        // 确保没有条目时计数器为0
        targetElfLibcVerneed->vn_cnt = 0;
    }

    // 循环结束后，更新 vn_cnt 计数器
    if (targetElfLibcVerneed && removed_count > 0)
    {
        targetElfLibcVerneed->vn_cnt -= removed_count;
    }

    for(const auto& patch : validIndexAndId_vct)
    {
        if(targetVersionInfo_vct[patch.originalTargetVersionIndex].id != patch.hostVersionId)
        {
            const char* dynsymName = targetElfPtrs->dynstr + targetElfPtrs->dynsym[patch.symbolIndex].st_name;

            printf("  修改 %-20s: %-12s(%02u) ----> %-12s(%02u)  文件:%-20s\n",
                   dynsymName,
                   targetVersionInfo_vct[patch.originalTargetVersionIndex].name.c_str(),
                   (unsigned short)targetVersionInfo_vct[patch.originalTargetVersionIndex].id,
                   patch.hostVersionName.c_str(),
                   (unsigned short)patch.hostVersionId,
                   path.c_str());

            targetElfPtrs->versions[patch.symbolIndex] = patch.hostVersionId;
        }
    }
    return true;
}

bool GlibcOper::checkFoundDynsym()
{
    bool ret = true;
    targetElfLibcVerneed = nullptr;
    Elf64_Verneed *verneed = targetElfPtrs->verneed;

    // 健壮性检查: 确保 verneed 不是 nullptr
    if (!verneed)
    {
        ErrorLog::getErrorLog()->putErrInfo("未能找到 .gnu.version_r 节区或内容为空", targetElfPtrs->getFilePath());
        return false;
    }

    while(true)
    {
        if(strcmp(targetElfPtrs->dynstr + verneed->vn_file, "libc.so.6"))
        {
            if(verneed->vn_next == 0) break;
            verneed = reinterpret_cast<Elf64_Verneed*>((char *)verneed + verneed->vn_next);
            continue;
        }

        targetElfLibcVerneed = verneed;

        // 健壮性检查: 确保 vn_aux 是有效的偏移
        if (verneed->vn_cnt > 0 && verneed->vn_aux != 0)
        {
            Elf64_Vernaux *naux = reinterpret_cast<Elf64_Vernaux*>((char *)verneed + verneed->vn_aux);
            while(true)
            {
                char *name = targetElfPtrs->dynstr + naux->vna_name;
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

    for (int i = 1; i < targetElfPtrs->sh_version->sh_size / sizeof(unsigned short); i++)
    {
        unsigned short v = targetElfPtrs->versions[i];
        unsigned hveridx = 0;
        for (; hveridx < targetVersionInfo_vct.size(); ++hveridx)
            if (v == targetVersionInfo_vct[hveridx].id) break;
        if (hveridx == targetVersionInfo_vct.size()) continue;

        const char* dynsymName = targetElfPtrs->dynstr + targetElfPtrs->dynsym[i].st_name;
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
                validIndexAndId_vct.push_back({i, (int)hveridx, verAndId.first, hostIdInTarget});
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
