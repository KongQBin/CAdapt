<p align="center">
  <a href="./README_en.md">English</a> &middot;
  <a href="./README.md">简体中文</a> &middot;
  <a href="./README_zh-Hant.md">繁體中文</a> &middot;
  <a href="./README_ja.md">日本語</a>
</p>

[![CI](https://github.com/KongQBin/CAdapt/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/KongQBin/CAdapt/actions/workflows/ci.yml)

# GLIBC & KMOD Symbol Version Adapter (CAdapt)

CAdapt patches ELF metadata on Linux to adjust symbol version requirements in compiled programs, shared libraries and kernel modules. It targets cases where ABI compatibility has already been established but version metadata prevents execution or loading.

## Features

- **GLIBC**: Read symbol versions from a supplied `libc.so.6` and adapt one ELF file or a directory. Keep the original version when available and preserve hidden symbol markers.
- **KMOD**: Modify `.ko` `vermagic` and symbol CRCs in the traditional `__versions` format.

## Build and test

Requires Linux, a GNU++11-capable C++ compiler and CMake 3.5+. Tests also require Python 3.7+ and `cc`.

```bash
cmake -S . -B build -DCADAPT_STRICT=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The language standard is fixed to **GNU++11**. `CADAPT_STRICT=ON` treats compiler warnings and language extensions as errors. Add `-DBUILD_TESTING=OFF` to build the tool without Python. With older CMake, run `cmake ..` and `ctest` inside the build directory. Build against the appropriate GLIBC/sysroot when targeting older systems.

## Usage

```bash
./build/cadapt -h

# GLIBC
./build/cadapt -c /path/to/libc.so.6 -t /path/to/target_elf_or_directory

# KMOD
./build/cadapt -m ./my_driver.ko -s ./Module.symvers \
  -v "4.18.0-193.el8.x86_64 SMP mod_unload"
```

### Options and behavior

- With `-s/--symvers`, CRCs come exclusively from that file; missing symbols do not silently fall back to the running kernel.
- Without `-s` or `-v`, collect CRCs or `vermagic` from current system modules on demand. An explicit `vermagic` must match the source of system CRCs. For another kernel, supply its `Module.symvers` and `vermagic`.
- KMOD writes begin only after all validation passes; validation failures leave the target unchanged. Synchronization failures return failure, but do not guarantee rollback of written data.
- Directory scans skip symbolic links. Help returns `0`; invalid arguments or adaptation failures return a nonzero exit code.

## Review, fixes and validation

**On 2026-10-12, OpenAI Codex assisted a round of code review, fixes and regression validation.** The changes are recorded in the [commit history](https://github.com/KongQBin/CAdapt/commits/main/), with [`4af3cbc`](https://github.com/KongQBin/CAdapt/commit/4af3cbc) as the validation baseline.

- Strict GNU++11 builds passed locally with **GCC 9.5 and GCC 15.2**, and all eight CTest checks passed.
- Coverage includes CLI behavior, GLIBC version selection and hidden bits, KMOD CRC/source conflicts, malformed ELF files, scan/synchronization failures, logging and resource ownership.
- Regression scripts are in [tests/](./tests/); the CI workflow is in [ci.yml](./.github/workflows/ci.yml). The badge above shows the remote workflow status.

This records AI-assisted review and observed test results; it is not an official OpenAI certification or a guarantee of compatibility with arbitrary files or ABIs.

## Supported formats and risks

- Supports **64-bit little-endian ELF with section headers**. ELF32, big-endian files and extended section numbering are unsupported.
- Extended KMOD sections `__version_ext_names` / `__version_ext_crcs` are explicitly rejected.
- Files are modified in place: back them up first. Metadata changes cannot check or repair ABI differences; applications may still crash and incompatible kernel modules may cause Kernel Panic/Oops.
- Errors go to `cadaptErrlog.txt` beside the executable, or stderr if the log cannot be written.
