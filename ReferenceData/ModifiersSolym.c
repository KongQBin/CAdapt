/* (c) 2012 Andrei Nigmatulin */
/* Modifiers solym(ymwh@foxmail.com) */
/* 用于去除对 GLIBC 的高版本依赖，将高版本符号替换到低版本 */

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

//  ELF文件解析（二）：ELF header详解
// https://www.cnblogs.com/jiqingwu/p/elf_explore_2.html
// ELF文件格式
// https://www.cntofu.com/book/114/Theory/ELF.md
// ELF文件格式解析(完)
// https://www.52pojie.cn/thread-591986-1-1.html

struct glibc_version_index {
    const char *version_str; // 标识版本的字符串
    unsigned version_idx;    // 记录这个版本的版本索引值
};

int process_elf(void *elf, size_t elf_sz) {
    // 获取 ELF文件头，在文件的开始，保存了路线图，描述了该文件的组织情况
    Elf64_Ehdr *elf_hdr = elf;
    // 获取 节头表(Section header table)
    Elf64_Shdr *sh = (Elf64_Shdr *)((char *)elf + elf_hdr->e_shoff);
    // 获取 节头字符串表
    Elf64_Shdr *sh_str = sh + elf_hdr->e_shstrndx;
    char *strtab = (char *)elf + sh_str->sh_offset;

    // 指向 .dynsym 表位置
    Elf64_Shdr *sh_dynsym = 0;
    Elf64_Sym *dynsym = 0;    // 指向符号信息表
    // 指向 .dynstr 表位置
    Elf64_Shdr *sh_dynstr = 0;
    char *dynstr = 0;
    // 指向 .gnu.version 表位置
    Elf64_Shdr *sh_version = 0;
    unsigned short *versions = 0; // 版本索引值列表
    // 指向 .gnu.version_r 表位置
    Elf64_Shdr *sh_version_r = 0;
    Elf64_Verneed *verneed = 0; // 用于遍历版本依赖节表

    unsigned i;
    // 遍历 节头 每一个 表
    for (i = 1; i < elf_hdr->e_shnum; i++) {
        // 获取指针
        Elf64_Shdr *this = sh + i;
        // 获取当前节名称
        char *name = strtab + this->sh_name;

        // 判断是否是 .dynsym 表
        // .dynsym表包含有关动态链接所需的所有符号的信息
        //  该表中的某个位置隐藏了依赖于该新glibc的函数的名称
        if (this->sh_type == SHT_DYNSYM && 0 == strcmp(name, ".dynsym")) {
            sh_dynsym = this;
            dynsym = (typeof(dynsym))((char *)elf + this->sh_offset);
            printf("找打 .dynsym section\n");
        }
        // 判断是否是 .dynstr 表
        // .dynstr 表包含实际版本的实际字符串（.gnu.version_r 仅是记录值）
        else if (this->sh_type == SHT_STRTAB && !strcmp(name, ".dynstr")) {
            sh_dynstr = this;
            dynstr = (typeof(dynstr))((char *)elf + this->sh_offset);
            printf("找到 .dynstr section\n");
        }
        // 判断是否是 .gnu.version 表
        // .gnu.version表它包含所有动态符号的版本信息
        // .dynsym中列出的每个符号都会在此处有一个对应的条目
        else if (this->sh_type == SHT_GNU_versym && !strcmp(name, ".gnu.version")) {
            sh_version = this;
            versions = (typeof(versions))((char *)elf + this->sh_offset);
            printf("找到 .gnu.version section\n");
        }
        // 判断是否是 .gnu.version_r 表
        // .gnu.version_r 表包含二进制文件所需的库版本（因此带有_r后缀）
        // 每个条目都显示版本名称（GLIBC_2.2.5，GLIBC_2.14等），并在其末尾显示“版本号”
        //“版本”号用词不准确，实际上是其他表可以用来引用它的索引。
        else if (this->sh_type == SHT_GNU_verneed &&
                 !strcmp(name, ".gnu.version_r")) {
            sh_version_r = this;
            verneed = (typeof(verneed))((char *)elf + this->sh_offset);
            printf("找到 .gnu.version_r section\n");
        }
    }

    if (!sh_dynsym || !sh_dynstr || !sh_version || !sh_version_r) {
        fprintf(stderr, "没有找到有效表节\n");
        return -1;
    }

    /* 记录 GLIBC_2.2.5 版本索引 */
    struct glibc_version_index glibc_2_2_5 = {"GLIBC_2.2.5", -1U};
    // 记录 高版本 在版本索引的位置
    struct glibc_version_index glibc_highver_arr[] = {
        /*{"GLIBC_2.3", -1U},   {"GLIBC_2.3.2", -1U}, {"GLIBC_2.3.3", -1U},
      {"GLIBC_2.3.4", -1U}, {"GLIBC_2.4", -1U},   {"GLIBC_2.5", -1U},
      {"GLIBC_2.6", -1U},   {"GLIBC_2.7", -1U},   {"GLIBC_2.8", -1U},
      {"GLIBC_2.9", -1U},*/   {"GLIBC_2.10", -1U},  {"GLIBC_2.11", -1U},
    {"GLIBC_2.12", -1U}, {"GLIBC_2.13", -1U}, {"GLIBC_2.14", -1U},
    {"GLIBC_2.15", -1U}, {"GLIBC_2.16", -1U}, {"GLIBC_2.17", -1U},
    {"GLIBC_2.18", -1U}, {"GLIBC_2.22", -1U}, {"GLIBC_2.23", -1U},
    {"GLIBC_2.24", -1U}, {"GLIBC_2.25", -1U}, {"GLIBC_2.26", -1U},
    {"GLIBC_2.27", -1U}, {"GLIBC_2.28", -1U}, {"GLIBC_2.29", -1U},
    {"GLIBC_2.30", -1U}, {"GLIBC_2.31", -1U}, {"GLIBC_2.32", -1U}};
    unsigned glibc_highver_count = sizeof(glibc_highver_arr)/sizeof(glibc_highver_arr[0]);

    /**
    * typedef struct {
    *         Elf64_Half      vn_version; // 此成员标识该结构的版本(0表示无效版本)
    *         Elf64_Half      vn_cnt;     // Elf64_Vernaux 数组中的元素数目
    *         Elf64_Word      vn_file;    // 以空字符结尾的字符串的字符串表偏移，用于提供版本依赖性的文件名。
    *                                     // 此名称与文件中找到的 .dynamic 依赖项之一匹配。
    *         Elf64_Word      vn_aux;     // 字节偏移，范围从此 Elf64_Verneed 项的开头到关联文件依赖项所需的版本定义的 Elf64_Vernaux
    *                                     // 数组。必须存在至少一种版本依赖性。也可以存在其他版本依赖性，具体数目由 vn_cnt 值表示。
    *         Elf64_Word      vn_next;    // 从此 Elf64_Verneed 项的开头到下一个 Elf64_Verneed 项的字节偏移
    * } Elf64_Verneed;
    */

    Elf64_Verneed *next_verneed;
    int last = 0;
    // 遍历 .gnu.version_r 表
    for (; !last; verneed = next_verneed) {
        // 获取依赖的文件名
        char *filename = dynstr + verneed->vn_file;
        // 获取下一个 表项
        next_verneed = (typeof(next_verneed))((char *)verneed + verneed->vn_next);
        // 判断当前是否是最后一个表项了
        last = verneed->vn_next == 0;
        // 依赖文件不是 libc.so.6，就跳过
        if (strcmp(filename, "libc.so.6")) {
            continue;
        }

        // 获取 Elf64_Vernaux 数组的结尾
        char *end_of_naux = (char *)next_verneed;
        if (last) {
            end_of_naux = (char *)elf + sh_version_r->sh_offset + sh_version_r->sh_size;
        }

        /**
    * typedef struct {
    *         Elf64_Word      vna_hash;    // 版本依赖性名称的散列值
    *         Elf64_Half      vna_flags;   // 版本依赖性特定信息(VER_FLG_WEAK[0x2]弱版本标识符)
    *         Elf64_Half      vna_other;   // 目前未使用
    *         Elf64_Word      vna_name;    // 以空字符结尾的字符串的字符串表偏移，用于提供版本依赖性的名称。
    *         Elf64_Word      vna_next;    // 从此 Elf64_Vernaux 项的开头到下一个 Elf64_Vernaux 项的字节偏移
    * } Elf64_Vernaux;
    */
        // 获取 Elf64_Vernaux 数组的元素数，首个元素
        unsigned cnt = verneed->vn_cnt;
        Elf64_Vernaux *naux = (typeof(naux))((char *)verneed + verneed->vn_aux);
        Elf64_Vernaux *next_naux;
        // 遍历 Elf64_Vernaux 数组（记录每一个依赖版本的信息）
        for (; cnt--; naux = next_naux)
        {
            char *name = dynstr + naux->vna_name; // GLIBC_xxx 字符串
            // 指向下一个元素
            next_naux = (typeof(next_naux))((char *)naux + naux->vna_next);
            printf("检查 %p %s %u\n", naux, name, naux->vna_next);
            // 如果是 GLIBC_2.2.5 记录下索引值
            if (strcmp(name, glibc_2_2_5.version_str) == 0)
            {
                glibc_2_2_5.version_idx = naux->vna_other;
                continue;
            }
            // 判断是否在高版本列表里面
            unsigned hveridx = 0;
            for (; hveridx < glibc_highver_count; ++hveridx)
            {
                if (strcmp(name, glibc_highver_arr[hveridx].version_str) == 0)
                {
                    break;
                }
            }
            // 如果属于高版本中的一个
            if (hveridx != glibc_highver_count)
            {
                // 记录下索引值
                glibc_highver_arr[hveridx].version_idx = naux->vna_other;
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
        // 处理完 libc.so.6 就跳出
        break;
    }

    if (glibc_2_2_5.version_idx == -1U) {
        fprintf(stderr, "无法找到 GLIBC_2.2.5 索引值\n");
        return -1;
    }
    for (i = 0; i < glibc_highver_count; ++i) {
        if (glibc_highver_arr[i].version_idx != -1U) {
            printf("%s 索引值: %d\n", glibc_highver_arr[i].version_str,glibc_highver_arr[i].version_idx);
        }
    }

    // 找到并修补所有依赖 GLIBC_2.xx 高版本号的符号
    for (i = 1; i < sh_version->sh_size / sizeof(unsigned short); i++)
    {
        unsigned short v = versions[i];
        // 判断版本是否为高版本
        unsigned hveridx = 0;
        for (; hveridx < glibc_highver_count; ++hveridx)
            if (v == glibc_highver_arr[hveridx].version_idx) break;
        // 不是就跳过
        if (hveridx == glibc_highver_count) continue;
        // 修改版本到 2.2.5
        printf("  修改 '%s': %s(%u) -> %s(%u)\n", dynstr + dynsym[i].st_name,
               glibc_highver_arr[hveridx].version_str,glibc_highver_arr[hveridx].version_idx,
               glibc_2_2_5.version_str,glibc_2_2_5.version_idx);
        // 修改高版本的到低版本
        versions[i] = glibc_2_2_5.version_idx; // 这里也可以写成 =0 (local defualt)
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "用法: %s <filename>\n", argv[0]);
        return 1;
    }
    // 打开输入文件
    int fd = open(argv[1], O_RDWR);

    if (0 > fd) {
        perror("打开文件失败");
        return 1;
    }

    // 获取文件状态信息，主要是获取文件大小
    struct stat st;
    if (0 > fstat(fd, &st)) {
        perror("获取文件状态失败");
        close(fd);
        return 1;
    }
    // 进行内存映射文件，便于后面处理的时候直接操作
    void *mem = mmap(0, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (mem == MAP_FAILED) {
        perror("内存映射文件失败");
        close(fd);
        return 1;
    }
    close(fd);
    // 处理文件
    process_elf(mem, st.st_size);
    // 将修改同步到文件
    if (0 > msync(mem, st.st_size, MS_SYNC)) {
        perror("msync() failed");
        return 1;
    }
    // 解除内存映射
    munmap(mem, st.st_size);
    return 0;
}
