#pragma once
#include <string>
#include <vector>
#include <map>
#include "ElfPtrs.h"
#include "ElfUtils.h"

// patchelf 式的文件重写：把要改的节区搬到文件末尾的新 LOAD 段里，
// 并修好所有指向它们的引用。用于新内容放不进原位置的场景
// （如更长的 RPATH 串、新增的 DT_RPATH 条目）。
//
// 做法（与 patchelf 的 rewriteSectionsLibrary 相同）：
//   * 新内容追加到文件末尾（按段对齐取整），虚址取所有 LOAD 段最高
//     虚址之上（按同样对齐取整），保证 p_vaddr ≡ p_offset (mod p_align)；
//   * 优先复用 PT_NULL 程序头槽位放新的 PT_LOAD，否则把程序头表整个
//     搬到新段里（e_phoff/e_phnum 更新；ET_EXEC 且原来没有 PT_PHDR 时
//     补一个，使旧内核也能通过 AT_PHDR 找到程序头表）；
//   * 被搬的节区更新 sh_offset/sh_addr/sh_size；PT_DYNAMIC/PT_INTERP
//     跟着各自的节区走；.dynamic 里指向被搬节区的指针项（DT_STRTAB、
//     DT_STRSZ 等）按新地址重算；
//   * 其余内容与全部偏移保持不动；先写同目录临时文件，验证通过后再
//     原子 rename 替换；重写会换 inode，符号链接目标会被拒绝。
//
// 不支持：静态文件（无程序头表）、DT_MIPS_RLD_MAP_REL 随 .dynamic
// 一起搬家（其值与自身槽位地址相关）、新段虚址超出目标地址空间。
// 调用方负责保证替换内容本身正确（如 .dynstr 须保留全部旧串偏移、
// .dynamic 须保留 DT_NULL 结尾），重写器只负责搬家与修引用。
template <typename Traits>
class ElfRewriter
{
public:
    ElfRewriter() = delete;

    // 要搬到新段里的节区：名字 -> 完整新内容。
    struct SectionReplacement
    {
        std::string name;
        std::string content;
    };

    // 对 path 做一次重写。dynamicUpdates：写到 .dynamic 里的 (tag -> 值)，
    // 如 (DT_RUNPATH -> 新串在 .dynstr 中的偏移)，在指针重算之前应用。
    // 成功返回 true；失败时原文件不动并记入错误日志。
    static bool rewriteFile(const std::string& path,
                            const std::vector<SectionReplacement>& replacements,
                            const std::map<typename Traits::Word, typename Traits::Xword>& dynamicUpdates);

private:
    // .dynamic 里指向被搬节区的指针项：tag -> 节区名；返回 "" 表示该
    // tag 不指向节区（或节区未被搬动），不用重算。DT_STRSZ 特殊处理。
    static std::string pointerSection(typename Traits::Word tag);
    // 对 .dynamic（位于 dynamicOffset 处，count 个条目）应用
    // dynamicUpdates，并把指向被搬节区的指针项按 newAddr 重算。
    // dynamicUpdates 里找不到的 tag 会返回 false。
    static bool fixDynamic(char* base, uint64_t dynamicOffset, size_t count,
                           const std::map<typename Traits::Word, typename Traits::Xword>& dynamicUpdates,
                           const std::map<std::string, uint64_t>& newAddrByName,
                           uint64_t newDynstrSize);
    // 验证重写后的临时文件：重读 + 结构检查。
    // newPosByName：节区名 -> (新文件偏移, 新虚址)。
    static bool verifyRewritten(const std::string& tmpPath, const std::string& path,
                                const std::vector<SectionReplacement>& replacements,
                                uint64_t startOffset, uint64_t startPage,
                                const std::map<std::string, std::pair<uint64_t, uint64_t>>& newPosByName,
                                const std::map<std::string, uint64_t>& newAddrByName);
};
