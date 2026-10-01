#include "KSymInfos.h"

using namespace std;
KSymInfos::KSymInfos()
{

}

KSymInfos::KSymInfos(string path)
{
    setPath(path);
    initInfos();
//    printInfos();
}

KSymInfos::~KSymInfos()
{

}

void KSymInfos::setPath(string path)
{
    m_path = path;
}

void KSymInfos::initInfos()
{
    ifstream modules(m_path);
    if (modules.is_open()) {
        string line;
        while (getline(modules, line)) {
            list<string> sym_info = splitString(line,'\t');
            if(sym_info.size() != 4) continue;

            KSymInfo info;

            string crcstr = sym_info.front();
            sym_info.pop_front();
            info.crc = stoul(crcstr,nullptr,16);

            info.name = sym_info.front();
            sym_info.pop_front();

            info.origin = sym_info.front();
            sym_info.pop_front();

            info.export_type = sym_info.front();
            sym_info.pop_front();

            m_infos.insert(make_pair(info.name,info));
        }
        modules.close(); // 关闭文件
    } else cerr << "Unable to open file." << endl;
}

void KSymInfos::printInfos()
{
    for(auto it : m_infos)
    {
        printf("%-16lu\t%-64s%-64s%-16s\n",it.second.crc,it.second.name.c_str(),
               it.second.origin.c_str(),it.second.export_type.c_str());
    }
}

unsigned long KSymInfos::findSym(string symname)
{
    unsigned long ret = 0;
    auto it = m_infos.find(symname);
    if(it != m_infos.end()) ret = it->second.crc;
    return ret;
}
