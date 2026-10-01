#include "ElfSymInfos.h"

using namespace std;
ElfSymInfos::ElfSymInfos(string file, ElfOpenMode mode, bool debug)
{
    m_eptrs = new ElfPtrs(mode,debug);
    m_eptrs->initPtrs(file);
    initModInfo();
    initSymInfo();
    m_debug = debug;
    if(m_debug)
    {
        printModInfo();
        printSymInfo();
    }
}

ElfSymInfos::~ElfSymInfos()
{
    if(m_eptrs) delete m_eptrs;
}

void ElfSymInfos::printModInfo()
{
    printf("ModInfo:\n");
    for(auto it : m_minfo)
        printf("\t%-64s\n",it);
}

void ElfSymInfos::printSymInfo()
{
    printf("SymInfo:\n");
    printf("\tnum_versions = %lu\n",m_vinfo.size());
    for(auto it : m_vinfo)
        printf("\tsym name = %-56s crc = 0x%lx\n",it.second->name,it.second->crc);
}

string ElfSymInfos::getVermagic()
{
    const char *vermagic = "vermagic=";
    for(auto it : m_minfo)
    {
        if(!strstr(it,vermagic)) continue;
        return string(it+strlen(vermagic));
    }
    return "";
}

map<string, unsigned long> ElfSymInfos::getSymVerInfo()
{
    map<string, unsigned long> ret;
    for(auto it : m_vinfo)
    {
        ret.insert(make_pair(it.first,it.second->crc));
    }
    return ret;
}

map<string, modversion_info *>* ElfSymInfos::getVInfo()
{
    return &m_vinfo;
}

list<char *>* ElfSymInfos::getMInfo()
{
    return &m_minfo;
}

bool ElfSymInfos::initModInfo()
{
    bool ret = false;
    do
    {
        const TableInfo *info = m_eptrs->getTableInfo(".modinfo");
        if(!info) break;
        if(!info->addr)
        {
            printf("info.addr is NULL!!!\n");
            break;
        }
        unsigned long size = info->addr->sh_size;
        /*
        * get_modinfo() calls made before rewrite_section_headers()
        * must use sh_offset, as sh_addr isn't set!
        */
        char *modinfo = (char *)m_eptrs->elf_hdr + info->addr->sh_offset;
        for (char *p = modinfo; p; p = nextString(p, &size)) {
            m_minfo.push_back(p);
        }
    }while(0);
    return ret;
}

static __u32 resolve_rel_crc(const __u32 *crc)
{
    return *(__u32 *)((char *)crc + *crc);
}

bool ElfSymInfos::initSymInfo()
{
    bool ret = false;
    do
    {
        const TableInfo *info = m_eptrs->getTableInfo("__versions");
        if(!info) break;
        if(!info->addr)
        {
            printf("info.addr is NULL!!!\n");
            break;
        }
        unsigned int num_versions = info->addr->sh_size / sizeof(struct modversion_info);
        modversion_info *versions = (modversion_info *)((char*)m_eptrs->elf_hdr + info->addr->sh_offset);
        if(!versions)
        {
            printf("versions is NULL!!!\n");
            break;
        }
        for(int i = 0; i < num_versions; i++) {
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
    }while(0);
    return ret;
}

/* Parse tag=value strings from .modinfo section */
char* ElfSymInfos::nextString(char *str, unsigned long *secsize)
{
    /* Skip non-zero chars */
    while (str[0]) {
        str++;
        if ((*secsize)-- <= 1)
            return NULL;
    }

    /* Skip any zero padding. */
    while (!str[0]) {
        str++;
        if ((*secsize)-- <= 1)
            return NULL;
    }
    return str;
}
