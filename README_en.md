<p align="center">
  <a href="./README_en.md">English</a>
  &middot;
  <a href="./README.md">简/</a>
  <a href="./README_zh-Hant.md">繁體中文</a>
  &middot;
  <a href="./README_ja.md">日本語</a>
</p>

# GLIBC Symbol Version Adapter
This project is an ELF utility designed to modify an ELF file, or all ELF files within a directory, to adapt them to a different version of GLIBC (libc.so.6).
The project concept is based on ReferenceData/ModifiersSolym.c (which contains comments that may be useful for reference).

**Key Difference:**
Unlike the reference file (which uses fixed, hard-coded versions), this project is more powerful:
1. It automatically parses all supported symbol versions from a user-provided libc.so.6 (argument 1).
2. It automatically patches the symbol version dependencies in the target ELF file or directory (argument 2) to be compatible with the provided libc.

**Usage:**
```bash
#./[program_name] [path/to/host/libc.so.6] [path/to/target_elf_or_directory]
./your_program /path/to/your/libc.so.6 /path/to/target_elf_or_directory
```

# WARNING: ABI COMPATIBILITY

- It does not, and cannot, check for or fix ABI (Application Binary Interface) incompatibilities caused by the GLIBC change.
- This tool only modifies the symbol version metadata within the ELF file (the .gnu.version and .gnu.version_r sections).
- If a function (e.g., memcpy or fopen) has different behavior, parameters, or internal data structures (struct) between the old and new libc versions, the program will very likely crash (e.g., Segmentation Fault) or produce corrupt data at runtime, even if the symbol version was "downgraded" successfully.
- This tool assumes YOU have independently verified that the ABIs between the old and new GLIBC versions are fully compatible. Use only if you know exactly what you are doing.

**Other Disclaimers and Risks**

- Symbol Dependency: Successful adaptation requires that all symbols used by the target ELF must physically exist in the provided (usually older) libc.so.6.
- Error Log: If adaptation fails, check the error log (e.g., Errlog.txt) in the program's directory. The log will specify which file and which symbol caused the failure.
- Stability Risk: There is a small chance of a segmentation fault when processing certain complex or unusual ELF files.
- BACKUP YOUR FILES: Although the tool has fault tolerance (using mmap and msync), given the low-level nature of this operation, it is strongly recommended to back up all target ELF files before use to prevent data loss.

