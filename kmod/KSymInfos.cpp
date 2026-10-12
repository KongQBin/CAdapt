#include "KSymInfos.h"
#include "ErrorLog.h"

namespace {
bool parseCrc(const std::string& text, unsigned long& crc)
{
    size_t start = text.compare(0, 2, "0x") == 0 || text.compare(0, 2, "0X") == 0 ? 2 : 0;
    if (start == text.size()) return false;
    crc = 0;
    for (size_t i = start; i < text.size(); ++i)
    {
        const char c = text[i];
        unsigned digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return false;
        if (crc > (0xffffffffUL - digit) / 16) return false;
        crc = crc * 16 + digit;
    }
    return true;
}
}

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
    m_infos.clear();
    m_valid = false;
}

bool KSymInfos::initInfos()
{
    m_valid = false;
    m_infos.clear();
    ifstream modules(m_path);
    if (!modules.is_open())
    {
        ErrorLog::getErrorLog()->putErrInfo("无法打开Module.symvers", m_path);
        cerr << "Unable to open Module.symvers: " << m_path << endl;
        return false;
    }
    map<string, KSymInfo> parsed;
    string line;
    size_t lineNumber = 0;
    while (getline(modules, line))
    {
        ++lineNumber;
        istringstream row(line);
        vector<string> fields;
        string field;
        while (row >> field) fields.push_back(field);
        if (fields.empty()) continue;
        KSymInfo info;
        if ((fields.size() != 4 && fields.size() != 5) || !parseCrc(fields[0], info.crc))
        {
            const string error = "Module.symvers行格式或CRC无效，行号:" + to_string(lineNumber);
            ErrorLog::getErrorLog()->putErrInfo(error, m_path);
            cerr << error << endl;
            return false;
        }
        info.name = fields[1];
        info.origin = fields[2];
        info.export_type = fields[3];
        // 第五列为可选namespace；CRC查找仍按符号名进行。
        auto existing = parsed.find(info.name);
        if (existing != parsed.end() && existing->second.crc != info.crc)
        {
            ErrorLog::getErrorLog()->putErrInfo("Module.symvers符号CRC冲突:" + info.name, m_path);
            return false;
        }
        parsed[info.name] = info;
    }
    if (modules.bad())
    {
        ErrorLog::getErrorLog()->putErrInfo("读取Module.symvers失败", m_path);
        return false;
    }
    m_infos.swap(parsed);
    m_valid = true;
    return true;
}

void KSymInfos::printInfos()
{
    for(auto it : m_infos)
    {
        printf("%-16lu\t%-64s%-64s%-16s\n",it.second.crc,it.second.name.c_str(),
               it.second.origin.c_str(),it.second.export_type.c_str());
    }
}

bool KSymInfos::findSym(const string& symname, unsigned long& crc) const
{
    auto it = m_infos.find(symname);
    if(it == m_infos.end()) return false;
    crc = it->second.crc;
    return true;
}
