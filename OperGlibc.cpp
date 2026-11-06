#include "OperGlibc.h"
#include "ElfUtils.h"
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <set>
#include <utility>

using namespace std;

namespace {

// 没有与目标匹配的 C 库时的统一警告（终端 + 错误日志）。
void warnNoMatchingLibc(const std::string& targetPath, const ElfIdentity& targetId,
                        const std::vector<std::string>& candidates)
{
    std::string message = "警告：没有与目标匹配的 C 库：目标为 " +
        ElfUtils::describeIdentity(targetId) + "，提供的 C 库为 ";
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (i) message += "、";
        ElfIdentity candidateId;
        if (ElfUtils::identifyElf(candidates[i], candidateId))
            message += ElfUtils::describeIdentity(candidateId);
        else
            message += "无法识别(" + candidates[i] + ")";
    }
    message += "，跳过：" + targetPath;
    std::cerr << message << std::endl;
    ErrorLog::getErrorLog()->putErrInfo(message, targetPath);
}

} // namespace

template <typename Traits>
bool OperGlibc::parseLibc(const std::string& glibcPath, ParsedLibc& out)
{
    ElfPtrs<Traits> glibcPtrs(ElfOpenMode::ReadOnly);
    if(!glibcPtrs.initPtrs(glibcPath.c_str()))
    {
        ErrorLog::getErrorLog()->putErrInfo("initGlibcPtrs失败", glibcPath);
        return false;
    }

    // 获取必需的表信息
    TableInfo<Traits> *tb_version_d = glibcPtrs.getTableInfo(".gnu.version_d");
    TableInfo<Traits> *tb_dynstr = glibcPtrs.getTableInfo(".dynstr");
    TableInfo<Traits> *tb_dynsym = glibcPtrs.getTableInfo(".dynsym");
    TableInfo<Traits> *tb_version = glibcPtrs.getTableInfo(".gnu.version");

    if(!tb_version_d || !tb_dynstr || !tb_dynsym || !tb_version)
    {
        ErrorLog::getErrorLog()->putErrInfo("获取Glibc必要节区失败", glibcPath);
        return false;
    }

    // 计算实际偏移
    char* ehdr = reinterpret_cast<char*>(glibcPtrs.elf_hdr);
    typename Traits::Verdef *verdef = reinterpret_cast<typename Traits::Verdef*>(ehdr + tb_version_d->addr->sh_offset);
    char *dynstr = ehdr + tb_dynstr->addr->sh_offset;
    typename Traits::Sym *dynsym = reinterpret_cast<typename Traits::Sym*>(ehdr + tb_dynsym->addr->sh_offset);
    unsigned short *versions = reinterpret_cast<unsigned short*>(ehdr + tb_version->addr->sh_offset);

    typename Traits::Verdef *next_verdef;
    set<string> seenVersions;

    for (int last = 0; !last; verdef = next_verdef)
    {
        if(!(verdef->vd_flags & VER_FLG_BASE))
        {
            typename Traits::Verdaux *daux = reinterpret_cast<typename Traits::Verdaux*>((char *)verdef + verdef->vd_aux);
            // 首个辅助项是版本名，其余辅助项为父版本，不应创建新的符号归属。
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
                        if((versions[i] & 0x7fff) == verdef->vd_ndx && dynsym[i].st_shndx != SHN_UNDEF)
                        {
                            string s_dynsym = string(dynstr + dynsym[i].st_name);
                            if(s_dynsym != name)
                                versionInfo.symbols.push_back(move(s_dynsym));
                        }
                    }
                    out.versions.push_back(move(versionInfo));
                }
            }
        }
        next_verdef = reinterpret_cast<typename Traits::Verdef*>((char *)verdef + verdef->vd_next);
        last = verdef->vd_next == 0;
    }
    for (const auto& version : out.versions)
    {
        out.versionIds[version.name] = version.id;
        for (const auto& symbol : version.symbols)
            out.symbolVersions[symbol].push_back({version.name, version.id});
    }
    showGlibcInfo(out);
    return true;
}

void OperGlibc::showGlibcInfo(const ParsedLibc& libc, bool showDynsym)
{
    const char* reset = "\033[0m";
    const char* bold = "\033[1m";
    const char* redBold = "\033[31;1m";

    if(showDynsym)
        cout << bold << "目标GLIBC所支持的版本及对应符号表如下:" << reset << endl;
    else
        cout << bold << "目标GLIBC所支持的版本如下:" << reset << endl;

    for (const auto& ver : libc.versions)
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

template <typename Traits>
bool OperGlibc::checkTarget(ElfPtrs<Traits>& targetPtrs, const ParsedLibc& libc,
                            typename Traits::Verneed*& libcVerneed,
                            std::vector<TargetVersionInfo>& targetVersions,
                            std::vector<SymbolPatchInfo>& patches)
{
    bool ret = true;
    libcVerneed = nullptr;

    TableInfo<Traits>* tb_version_r = targetPtrs.getTableInfo(".gnu.version_r");
    TableInfo<Traits>* tb_dynstr = targetPtrs.getTableInfo(".dynstr");
    TableInfo<Traits>* tb_version = targetPtrs.getTableInfo(".gnu.version");
    TableInfo<Traits>* tb_dynsym = targetPtrs.getTableInfo(".dynsym");

    if(!tb_version_r || !tb_dynstr || !tb_version || !tb_dynsym)
    {
        ErrorLog::getErrorLog()->putErrInfo("目标ELF缺少必要节区", targetPtrs.getFilePath());
        return false;
    }

    char* ehdr = reinterpret_cast<char*>(targetPtrs.elf_hdr);
    typename Traits::Verneed *verneed = reinterpret_cast<typename Traits::Verneed*>(ehdr + tb_version_r->addr->sh_offset);
    char *dynstr = ehdr + tb_dynstr->addr->sh_offset;
    unsigned short *versions = reinterpret_cast<unsigned short*>(ehdr + tb_version->addr->sh_offset);
    typename Traits::Sym *dynsym = reinterpret_cast<typename Traits::Sym*>(ehdr + tb_dynsym->addr->sh_offset);

    while(true)
    {
        if(strcmp(dynstr + verneed->vn_file, "libc.so.6"))
        {
            if(verneed->vn_next == 0) break;
            verneed = reinterpret_cast<typename Traits::Verneed*>((char *)verneed + verneed->vn_next);
            continue;
        }

        libcVerneed = verneed;

        if (verneed->vn_cnt > 0 && verneed->vn_aux != 0)
        {
            typename Traits::Vernaux *naux = reinterpret_cast<typename Traits::Vernaux*>((char *)verneed + verneed->vn_aux);
            while(true)
            {
                char *name = dynstr + naux->vna_name;
                targetVersions.push_back({string(name), naux->vna_other & 0x7fff});

                if(naux->vna_next == 0) break;
                naux = reinterpret_cast<typename Traits::Vernaux*>((char *)naux + naux->vna_next);
            }
        }
        break;
    }
    if(!libcVerneed)
    {
        ErrorLog::getErrorLog()->putErrInfo("未能找到libc.so.6所在节表", targetPtrs.getFilePath());
        return false;
    }

    for (size_t i = 1; i < tb_version->addr->sh_size / sizeof(unsigned short); i++)
    {
        unsigned short v = versions[i] & 0x7fff;
        unsigned hveridx = 0;
        for (; hveridx < targetVersions.size(); ++hveridx)
            if (v == targetVersions[hveridx].id) break;
        if (hveridx == targetVersions.size()) continue;

        const char* dynsymName = dynstr + dynsym[i].st_name;
        auto verAndId = findSymbol(libc, string(dynsymName), targetVersions[hveridx].name, targetVersions);
        if(verAndId.second == -1)
        {
            ErrorLog::getErrorLog()->putErrInfo(
                string("目标C库中未找到符号:") + dynsymName + "    GLIBC版本:" + targetVersions[hveridx].name,
                targetPtrs.getFilePath());
            ret = false;
        }
        else
        {
            int hostIdInTarget = -1;
            for(const auto& vInfo : targetVersions)
            {
                if(vInfo.name == verAndId.first)
                {
                    hostIdInTarget = vInfo.id;
                    break;
                }
            }

            if(hostIdInTarget != -1)
            {
                patches.push_back({i, (int)hveridx, verAndId.first, hostIdInTarget});
            }
            else
            {
                ErrorLog::getErrorLog()->putErrInfo("未能找到目标elf中对应版本的id", targetPtrs.getFilePath());
                ret = false;
            }
        }
    }
    return ret;
}

template <typename Traits>
bool OperGlibc::adaptTargetFile(const std::string& path, const ParsedLibc& libc)
{
    if (libc.versions.empty()) return false;

    unique_ptr<ElfPtrs<Traits>> targetPtrs(new ElfPtrs<Traits>(ElfOpenMode::ReadWrite));
    if(!targetPtrs->initPtrs(path.c_str()))
    {
        ErrorLog::getErrorLog()->putErrInfo("initTargetPtrs失败", path);
        return false;
    }

    typename Traits::Verneed* libcVerneed = nullptr;
    std::vector<TargetVersionInfo> targetVersions;
    std::vector<SymbolPatchInfo> patches;
    if(!checkTarget(*targetPtrs, libc, libcVerneed, targetVersions, patches)) return false;

    unsigned removed_count = 0;
    unsigned original_cnt = 0;

    if (libcVerneed)
    {
        original_cnt = libcVerneed->vn_cnt;
    }

    TableInfo<Traits>* tb_dynstr = targetPtrs->getTableInfo(".dynstr");
    char* target_dynstr = tb_dynstr ? (reinterpret_cast<char*>(targetPtrs->elf_hdr) + tb_dynstr->addr->sh_offset) : nullptr;

    if(!target_dynstr) return false;

    if (libcVerneed && original_cnt > 0 && libcVerneed->vn_aux != 0)
    {
        typename Traits::Vernaux *naux = reinterpret_cast<typename Traits::Vernaux *>((char *)libcVerneed + libcVerneed->vn_aux);
        typename Traits::Vernaux *prev_naux = nullptr;

        for (unsigned cnt = 0; cnt < original_cnt; ++cnt)
        {
            char *name = target_dynstr + naux->vna_name;

            typename Traits::Vernaux *next_naux = nullptr;
            if (naux->vna_next != 0)
                next_naux = reinterpret_cast<typename Traits::Vernaux*>((char *)naux + naux->vna_next);

            if(findVersion(libc, name).second == -1)
            {
                removed_count++;
                if (prev_naux)
                {
                    // vna_next 相对于所在节点，跳过当前节点时需从保留节点重新计算。
                    prev_naux->vna_next = next_naux
                        ? static_cast<typename Traits::Word>(reinterpret_cast<char*>(next_naux) - reinterpret_cast<char*>(prev_naux))
                        : 0;
                }
                else
                {
                    if (next_naux)
                    {
                        libcVerneed->vn_aux = (char*)next_naux - (char*)libcVerneed;
                    }
                    else
                    {
                        libcVerneed->vn_aux = 0;
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
    else if (libcVerneed)
    {
        libcVerneed->vn_cnt = 0;
    }

    if (libcVerneed && removed_count > 0)
    {
        libcVerneed->vn_cnt -= removed_count;
    }

    TableInfo<Traits>* tb_version = targetPtrs->getTableInfo(".gnu.version");
    TableInfo<Traits>* tb_dynsym = targetPtrs->getTableInfo(".dynsym");

    char* target_ehdr = reinterpret_cast<char*>(targetPtrs->elf_hdr);
    unsigned short *target_versions = reinterpret_cast<unsigned short*>(target_ehdr + tb_version->addr->sh_offset);
    typename Traits::Sym *target_dynsym = reinterpret_cast<typename Traits::Sym*>(target_ehdr + tb_dynsym->addr->sh_offset);

    for(const auto& patch : patches)
    {
        if(targetVersions[patch.originalTargetVersionIndex].id != patch.hostVersionId)
        {
            const char* dynsymName = target_dynstr + target_dynsym[patch.symbolIndex].st_name;

            printf("  修改 %-20s: %-12s(%02u) ----> %-12s(%02u)  文件:%-20s\n",
                   dynsymName,
                   targetVersions[patch.originalTargetVersionIndex].name.c_str(),
                   (unsigned short)targetVersions[patch.originalTargetVersionIndex].id,
                   patch.hostVersionName.c_str(),
                   (unsigned short)patch.hostVersionId,
                   path.c_str());

            // 保留符号原有的隐藏标记，只替换低15位的版本编号。
            target_versions[patch.symbolIndex] = (target_versions[patch.symbolIndex] & 0x8000) | patch.hostVersionId;
        }
    }
    return targetPtrs->flush();
}

template <typename Traits>
FileAdaptResult OperGlibc::adaptOneFileFor(const std::string& path, const std::vector<std::string>& libcPaths)
{
    typename Traits::Half machine = 0;
    {
        ElfPtrs<Traits> probe(ElfOpenMode::ReadOnly);
        if (!probe.initPtrs(path))
        {
            ErrorLog::getErrorLog()->putErrInfo("目标不是有效的 ELF 文件", path);
            return FileAdaptResult::Failed;
        }
        machine = probe.elf_hdr->e_machine;
    }

    // 按命令行顺序挑候选：身份（类别/字节序/架构）匹配且能解析成功的
    // 第一个 libc；解析失败时继续试下一个匹配的候选。
    const ParsedLibc* chosen = nullptr;
    for (const auto& candidate : libcPaths)
    {
        ElfIdentity id;
        if (!ElfUtils::identifyElf(candidate, id)) continue;
        if (id.elfClass != Traits::elfClass || id.elfData != ELFDATA2LSB || id.machine != machine) continue;
        auto it = m_libcCache.find(candidate);
        if (it == m_libcCache.end())
        {
            ParsedLibc parsed;
            if (!parseLibc<Traits>(candidate, parsed)) continue;
            it = m_libcCache.emplace(candidate, std::move(parsed)).first;
        }
        chosen = &it->second;
        break;
    }
    if (!chosen)
    {
        ElfIdentity targetId;
        ElfUtils::identifyElf(path, targetId);
        warnNoMatchingLibc(path, targetId, libcPaths);
        return FileAdaptResult::Skipped;
    }
    return adaptTargetFile<Traits>(path, *chosen) ? FileAdaptResult::Patched : FileAdaptResult::Failed;
}

FileAdaptResult OperGlibc::adaptOneFile(const std::string& path, const std::vector<std::string>& libcPaths)
{
    ElfIdentity id;
    if (!ElfUtils::identifyElf(path, id))
    {
        ElfUtils::logIdentifyFailure(path);
        return FileAdaptResult::Failed;
    }
    if (id.elfData == ELFDATA2MSB)
    {
        ElfUtils::warnUnsupportedEndian(path, id);
        return FileAdaptResult::Skipped;
    }
    if (id.elfClass == ELFCLASS64) return adaptOneFileFor<Elf64Traits>(path, libcPaths);
    return adaptOneFileFor<Elf32Traits>(path, libcPaths);
}

bool OperGlibc::adaptedTargets(const string& path, const vector<string>& libcPaths)
{
    // 先校验候选 libc 是否可读：缺失/损坏的候选在处理目标前就应报错
    // （与旧版先解析 libc 再处理目标的行为一致）；能识别但解析失败
    // 的候选在挑中时再记错，继续试下一个匹配的候选。
    for (const auto& candidate : libcPaths)
    {
        ElfIdentity id;
        if (!ElfUtils::identifyElf(candidate, id)) ElfUtils::logIdentifyFailure(candidate);
    }

    if (ElfUtils::isDirectory(path))
    {
        vector<string> files;
        if (!ElfUtils::collectElfFiles(path, files)) return false;

        unsigned patched = 0;
        unsigned skipped = 0;
        bool success = true;
        for (const auto& str : files)
        {
            const FileAdaptResult result = adaptOneFile(str, libcPaths);
            if (result == FileAdaptResult::Patched) ++patched;
            else if (result == FileAdaptResult::Skipped) ++skipped;
            else success = false;
        }
        if (patched == 0)
        {
            ErrorLog::getErrorLog()->putErrInfo("目录中没有可适配的 ELF 文件", path);
            return false;
        }
        (void)skipped;
        return success;
    }
    return adaptOneFile(path, libcPaths) == FileAdaptResult::Patched;
}

pair<string, int> OperGlibc::findVersion(const ParsedLibc& libc, const string& version)
{
    auto it = libc.versionIds.find(version);
    if (it != libc.versionIds.end()) return {it->first, it->second};
    return {"", -1};
}

pair<string, int> OperGlibc::findSymbol(const ParsedLibc& libc, const string& dynsym, const string& preferredVersion,
                                        const vector<TargetVersionInfo>& targetVersions)
{
    auto found = libc.symbolVersions.find(dynsym);
    if (found == libc.symbolVersions.end()) return {"", -1};
    pair<string, int> available = {"", -1};
    for (const auto& version : found->second)
    {
        if (version.first == preferredVersion) return version;
        if (available.second == -1)
            for (const auto& target : targetVersions)
                if (target.name == version.first) { available = version; break; }
    }
    return available.second != -1 ? available : found->second.front();
}
