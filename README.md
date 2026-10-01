<p align="center">
  <a href="./README_en.md">EN</a>
  &middot;
  <a href="./README.md">简/</a>
  <a href="./README_zh-Hant.md">繁</a>
  &middot;
  <a href="./README_ja.md">日</a>
</p>
<p align="center">
  <a href="https://hits.sh/github.com/KongQBin/CAdapt/"><img alt="Hits" src="https://hits.sh/github.com/KongQBin/CAdapt.svg?label=%E7%82%B9%E5%87%BB%E9%87%8F&color=007ec6"/></a>
</p>

# GLIBC & KMOD 符号版本适配工具 (CAdapt)

本项目是一个底层的 ELF/内核模块 修补工具，主要用于解决特定场景下已编译的可执行程序、动态库或驱动程序由于系统版本不匹配而无法运行或加载的问题。

本项目支持两大核心功能：
1. **GLIBC 适配**：解析指定的 `libc.so.6` 所支持的符号，并自动修补目标 ELF 文件（或目录）的符号版本依赖，使其与提供的 libc 兼容。
2. **KMOD 适配**：修改 Linux 内核模块 (`.ko`) 的 `vermagic` 和内核符号 CRC 校验值，绕过内核加载检查，使其能够强制加载到版本不完全匹配的内核中。

**使用方法 (Usage):**
```bash
# 【GLIBC 适配模式】
# ./[程序名] -c [宿主libc.so.6的路径] -t [要适配的ELF文件或目录路径]
./cadapt -c /path/to/your/libc.so.6 -t /path/to/target_elf_or_directory

# 【KMOD (内核模块) 适配模式】
# ./[程序名] -m [要修改的.ko文件] -s [可选: Module.symvers路径] -v [可选: 目标vermagic字符串]
./cadapt -m ./my_driver.ko -s ./Module.symvers -v "4.18.0-193.el8.x86_64 SMP mod_unload"
# 注：若不提供 -s 或 -v，程序将尝试自动从当前运行的系统中提取相关 CRC 和魔术字符串信息。
```

# ⚠️ 警告：关于 ABI 兼容性与内核崩溃风险
- 仅修改元数据：本项目 只修改 ELF 或 KO 文件中的符号版本元数据（如 .gnu.version 节区或符号 CRC 校验码），不会且无法检查或修复由于底层版本变更引起的 ABI (应用程序二进制接口) 不兼容问题。

- GLIBC 崩溃风险：如果函数（例如 memcpy 或 fopen）在新旧 libc 间的行为、参数或其内部使用的数据结构发生了实质性变化，程序在运行时极有可能因 ABI 不匹配而触发段错误 (Segmentation Fault) 或产生不可预期的脏数据。

- KMOD 崩溃风险 (Kernel Panic)：强制修改 .ko 的 CRC 和 vermagic 绕过了内核的安全检查机制。如果内核导出符号（如核心结构体布局）发生了改变，强行加载修改后的模块将直接导致内核崩溃 (Kernel Panic / Oops)。

- 此工具假定您已自行确认新旧环境间的 ABI 是完全兼容的。请仅在您确切知道自己在做什么的情况下使用，并在操作前务必备份原文件。
