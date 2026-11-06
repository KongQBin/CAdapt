#include "ElfSymInfos.h"

using namespace std;

template <typename Traits>
bool ElfSymInfos<Traits>::initModInfo()
{
    bool ret = false;
    do
    {
        const TableInfo<Traits> *info = m_eptrs->getTableInfo(".modinfo");
        if(!info) break;
        if(!info->addr)
        {
            printf("info.addr is NULL!!!\n");
            break;
        }
        unsigned long size = info->addr->sh_size;
        if (!size) break;
        /*
        * get_modinfo() calls made before rewrite_section_headers()
        * must use sh_offset, as sh_addr isn't set!
        */
        char *modinfo = (char *)m_eptrs->elf_hdr + info->addr->sh_offset;
        for (char *p = modinfo; size > 0;) {
            char *end = static_cast<char*>(memchr(p, '\0', size));
            if (!end) return false;
            if (*p) m_minfo.push_back(p);
            const size_t consumed = end - p + 1;
            size -= consumed;
            p = end + 1;
        }
        ret = true;
    }while(0);
    return ret;
}

template <typename Traits>
bool ElfSymInfos<Traits>::initSymInfo()
{
    bool ret = false;
    do
    {
        if (m_eptrs->getTableInfo("__version_ext_names") || m_eptrs->getTableInfo("__version_ext_crcs"))
        {
            ErrorLog::getErrorLog()->putErrInfo("暂不支持扩展KMOD符号版本格式，拒绝修改", m_eptrs->getFilePath());
            fprintf(stderr, "Unsupported extended KMOD version sections; target was not modified.\n");
            return false;
        }
        const TableInfo<Traits> *info = m_eptrs->getTableInfo("__versions");
        // 未启用符号版本校验的模块可以没有 __versions。
        if(!info) return true;
        if(!info->addr)
        {
            printf("info.addr is NULL!!!\n");
            break;
        }
        // __versions 条目宽度随 ELF 类别变化（32 位 60 字节，64 位 64 字节）。
        if (info->addr->sh_size % sizeof(ModVersionInfo<Traits>) != 0) break;
        size_t num_versions = info->addr->sh_size / sizeof(ModVersionInfo<Traits>);
        ModVersionInfo<Traits> *versions = (ModVersionInfo<Traits> *)((char*)m_eptrs->elf_hdr + info->addr->sh_offset);
        if(!versions)
        {
            printf("versions is NULL!!!\n");
            break;
        }
        for(size_t i = 0; i < num_versions; i++) {
            if (!memchr(versions[i].name, '\0', sizeof(versions[i].name))) return false;
            m_vinfo.insert(make_pair(versions[i].name,&(versions[i])));
            /*                  内核的校验流程                 */
            //        u32 crcval;
            //        if (strcmp(versions[i].name, symname) != 0)
            //            continue;
            //        if (IS_ENABLED(CONFIG_MODULE_REL_CRCS))
            //            crcval = resolve_rel_crc(crc);
            //        else
            //            crcval = *crc;
            //        if (versions[i].crc == crcval)
            //            return 1;
            //        pr_debug("Found checksum %X vs module %lX\n",
            //                 crcval, versions[i].crc);
            //        goto bad_version;
        }
        ret = true;
    }while(0);
    return ret;
}

template <typename Traits>
string ElfSymInfos<Traits>::getVermagic()
{
    const char *vermagic = "vermagic=";
    for(auto it : m_minfo)
    {
        if(strncmp(it,vermagic,strlen(vermagic)) != 0) continue;
        return string(it+strlen(vermagic));
    }
    return "";
}

template <typename Traits>
ElfSymInfos<Traits>::ElfSymInfos(string file, ElfOpenMode mode, bool debug)
    : m_debug(debug), m_eptrs(new ElfPtrs<Traits>(mode,debug))
{
    if (!m_eptrs->initPtrs(file)) return;
    if (m_eptrs->elf_hdr->e_type != ET_REL ||
        !initModInfo() || !initSymInfo() || getVermagic().empty())
    {
        ErrorLog::getErrorLog()->putErrInfo("内核模块初始化失败：需要有效的 .modinfo/vermagic 和符号版本信息", file);
        m_minfo.clear();
        m_vinfo.clear();
        return;
    }
    m_valid = true;
    if(m_debug)
    {
        printModInfo();
        printSymInfo();
    }
}

template <typename Traits>
void ElfSymInfos<Traits>::printModInfo()
{
    printf("ModInfo:\n");
    for(auto it : m_minfo)
        printf("\t%-64s\n",it);
}

template <typename Traits>
void ElfSymInfos<Traits>::printSymInfo()
{
    printf("SymInfo:\n");
    printf("\tnum_versions = %lu\n",m_vinfo.size());
    for(auto it : m_vinfo)
        printf("\tsym name = %-56s crc = 0x%lx\n",it.second->name,(unsigned long)it.second->crc);
}

template <typename Traits>
map<string, unsigned long> ElfSymInfos<Traits>::getSymVerInfo()
{
    map<string, unsigned long> ret;
    for(auto it : m_vinfo)
    {
        ret.insert(make_pair(it.first,it.second->crc));
    }
    return ret;
}

template <typename Traits>
map<string, ModVersionInfo<Traits>*>* ElfSymInfos<Traits>::getVInfo()
{
    return &m_vinfo;
}

template <typename Traits>
list<char *>* ElfSymInfos<Traits>::getMInfo()
{
    return &m_minfo;
}

// 显式实例化：其余翻译单元只通过声明使用这两个实例化，定义集中在此。
template class ElfSymInfos<Elf64Traits>;
template class ElfSymInfos<Elf32Traits>;
