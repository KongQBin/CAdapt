<p align="center">
  <a href="./README_en.md">English</a> &middot;
  <a href="./README.md">简体中文</a> &middot;
  <a href="./README_zh-Hant.md">繁體中文</a> &middot;
  <a href="./README_ja.md">日本語</a>
</p>

[![CI](https://github.com/KongQBin/CAdapt/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/KongQBin/CAdapt/actions/workflows/ci.yml)

<p align="center">
  <a href="https://hits.sh/github.com/KongQBin/CAdapt/"><img alt="Hits" src="https://hits.sh/github.com/KongQBin/CAdapt.svg?label=%E7%82%B9%E5%87%BB%E9%87%8F&color=007ec6"/></a>
</p>

# GLIBC & KMOD 符号版本适配工具（CAdapt）

CAdapt 是面向 Linux 的 ELF 元数据修补工具，可调整已编译程序、动态库和内核模块的符号版本要求。适用于已确认 ABI 兼容、但版本元数据阻止运行或加载的场景。

## 功能

- **GLIBC**：读取指定 `libc.so.6` 的符号版本，适配单个 ELF 或目录；原版本可用时优先保留，保留符号隐藏标记。
- **KMOD**：修改 `.ko` 的 `vermagic` 与传统 `__versions` 格式的符号 CRC。

## 构建与检查

需要 Linux、支持 GNU++11 的 C++ 编译器和 CMake 3.5+。测试额外需要 Python 3.7+ 与 `cc`。

```bash
cmake -S . -B build -DCADAPT_STRICT=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

语言标准固定为 **GNU++11**。`CADAPT_STRICT=ON` 将编译警告和语言扩展视为错误。仅构建工具可加 `-DBUILD_TESTING=OFF`，无需 Python。较旧 CMake 可进入构建目录执行 `cmake ..` 和 `ctest`。在旧系统运行时，请使用对应的 GLIBC/sysroot 构建。

## 使用

```bash
./build/cadapt -h

# GLIBC
./build/cadapt -c /path/to/libc.so.6 -t /path/to/target_elf_or_directory

# KMOD
./build/cadapt -m ./my_driver.ko -s ./Module.symvers \
  -v "4.18.0-193.el8.x86_64 SMP mod_unload"
```

### 参数与行为

- 提供 `-s/--symvers` 时，CRC 仅取自该文件；缺失符号不会从当前内核静默兜底。
- 省略 `-s` 或 `-v` 时，按需采集当前系统模块的 CRC 或 `vermagic`。使用系统 CRC 时，显式指定的 `vermagic` 必须与该来源一致；适配其他内核请提供对应的 `Module.symvers` 和 `vermagic`。
- KMOD 完整校验后才写入；校验失败时目标保持不变。同步写入失败会返回失败，但不保证回滚已写入内容。
- 目录扫描跳过符号链接。正常帮助返回 `0`，参数或适配失败返回非零退出码。

## 审查、修复与验证

**2026-10-12，项目借助 OpenAI Codex 完成了一轮代码审查、问题修复和回归验证。** 修复记录可在[提交历史](https://github.com/KongQBin/CAdapt/commits/main/)中查阅，验证基线为 [`4af3cbc`](https://github.com/KongQBin/CAdapt/commit/4af3cbc)。

- 本地使用 **GCC 9.5 和 GCC 15.2** 完成 GNU++11 严格编译，八个 CTest 检查全部通过。
- 检查覆盖命令行、GLIBC 多版本与隐藏位、KMOD CRC/来源冲突、异常 ELF、扫描与同步失败、日志和资源所有权。
- 自动回归脚本位于 [tests/](./tests/)，CI 配置位于 [ci.yml](./.github/workflows/ci.yml)；远程运行状态见页面顶部徽章。

上述说明记录的是 AI 辅助审查与实际测试结果，不代表 OpenAI 官方认证，也不保证任意文件或 ABI 的兼容性。

## 支持范围与风险

- 当前支持带节头表的 **64 位小端 ELF**；不支持 32 位、大端或扩展节区编号。
- 扩展 KMOD 节区 `__version_ext_names` / `__version_ext_crcs` 会明确拒绝修改。
- 工具原地修改文件，请先备份。仅修改元数据，无法检查或修复 ABI 差异；程序仍可能崩溃，错误的内核模块可能引发 Kernel Panic/Oops。
- 错误日志写入可执行文件目录下的 `cadaptErrlog.txt`；日志不可写时会输出到 stderr。
