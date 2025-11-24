<p align="center">
  <a href="./README_en.md">English</a>
  &middot;
  <a href="./README.md">简/</a>
  <a href="./README_zh-Hant.md">繁體中文</a>
  &middot;
  <a href="./README_ja.md">日本語</a>
</p>

# GLIBC 符号版本适配工具
本项目是一个ELF工具，用于修改ELF文件或指定目录中的所有ELF文件，使其适应不同版本的 GLIBC (`libc.so.6`)。
项目思路参考自 `ReferenceData/ModifiersSolym.c` 文件（该文件中包含了一些注释，可供参考）。

**主要差异：**
与参考文件（使用固定的版本号）不同，本项目功能更强大：
	1. 可以自动从用户指定的 `libc.so.6` (参数1) 中解析其支持的所有符号版本。
	2. 自动修补目标ELF文件或目录 (参数2) 所需的符号版本依赖，使其与参数1的 `libc` 兼容。

**使用方法 (Usage):**
```bash
 #./[程序名] [宿主libc.so.6的路径] [要适配的ELF文件或目录路径]
 ./your_program /path/to/your/libc.so.6 /path/to/target_elf_or_directory
```
# 警告：关于ABI兼容性
- 本项目 只修改 ELF 文件中的符号版本元数据（.gnu.version 和 .gnu.version_r 节区）。
- 本项目 不会，也无法 检查或修复由于 GLIBC 版本变更引起的 ABI (应用程序二进制接口) 不兼容 问题。
- 如果一个函数（例如 memcpy 或 fopen）在旧版本和新版本 libc 间的行为、参数、或其内部使用的数据结构（struct）发生了变化，那么即使符号版本被成功“降级”，程序在运行时也极有可能因ABI不匹配而崩溃（如段错误）或产生不可预期的错误数据。
- 此工具假定您已自行确认新旧 GLIBC 版本间的 ABI 是完全兼容的。请仅在您确切知道自己在做什么的情况下使用。

**其他注意事项与风险**

- 符号依赖: 适配成功的前提是：目标ELF所依赖的所有符号必须在您提供的（通常是低版本的）libc.so.6 中 物理存在。
- 错误日志: 如果适配失败，请检查程序目录下的错误日志 (例如 Errlog.txt)。日志会指明哪个文件的哪个符号导致了失败。
- 稳定性风险: 对于结构特殊的ELF文件，适配过程有小概率触发段错误。
- 数据备份: 虽然程序对文件操作进行了容错处理（使用 mmap 和 msync），但鉴于此工具的底层修改特性，强烈建议在使用前备份所有目标ELF文件，以防万一。

