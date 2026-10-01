<p align="center">
  <a href="./README_en.md">EN</a>
  &middot;
  <a href="./README.md">简/</a>
  <a href="./README_zh-Hant.md">繁</a>
  &middot;
  <a href="./README_ja.md">日</a>
</p>

# GLIBC & KMOD Symbol Version Adapter (CAdapt)

This project is a low-level ELF/Kernel Module patching utility designed to resolve issues where compiled executables, dynamic libraries, or kernel drivers fail to run or load due to system version mismatches in specific scenarios.

The project supports two core features:
1. **GLIBC Adaptation**: Automatically parses supported symbol versions from a provided `libc.so.6` and patches the symbol version dependencies in the target ELF file (or directory) to be compatible.
2. **KMOD Adaptation**: Modifies the `vermagic` and kernel symbol CRC checksums of Linux Kernel Modules (`.ko`), bypassing kernel load checks and allowing them to be forced-loaded into kernels with mismatched versions.

**Usage:**
```bash
# [GLIBC Adaptation Mode]
# ./[program] -c [path/to/host/libc.so.6] -t [path/to/target_elf_or_directory]
./cadapt -c /path/to/your/libc.so.6 -t /path/to/target_elf_or_directory

# [KMOD Adaptation Mode]
# ./[program] -m [target .ko file] -s [optional: path to Module.symvers] -v [optional: target vermagic string]
./cadapt -m ./my_driver.ko -s ./Module.symvers -v "4.18.0-193.el8.x86_64 SMP mod_unload"
# Note: If -s or -v are omitted, the program will attempt to extract the CRC and vermagic information from the currently running system.
```

# ⚠️ WARNING: ABI Compatibility & Kernel Panic Risks
- Metadata Modification Only: This tool only modifies symbol version metadata (e.g., .gnu.version sections or symbol CRC checksums). It does not, and cannot, check for or fix ABI (Application Binary Interface) incompatibilities.

- GLIBC Crash Risk: If a function (e.g., memcpy or fopen) has different behavior, parameters, or internal data structures between libc versions, the program is highly likely to crash (e.g., Segmentation Fault) or produce corrupt data at runtime.

- KMOD Crash Risk (Kernel Panic): Forcing CRC and vermagic modifications bypasses the kernel's safety checks. If exported kernel symbols (e.g., core struct layouts) have changed, forcing the module to load will directly result in a Kernel Panic / Oops.

- This tool assumes YOU have independently verified that the ABIs between the two environments are fully compatible. Use only if you know exactly what you are doing, and ALWAYS BACKUP your files before operation.
