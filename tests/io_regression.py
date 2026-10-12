"""Run with: python3 tests/io_regression.py /path/to/cadapt"""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


binary = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="cadapt-io-test-") as directory:
    root = pathlib.Path(directory)

    def compile_file(name, source, flags):
        code = root / (name + ".c")
        output = root / name
        code.write_text(source)
        subprocess.run(["cc", str(code), *flags, "-o", str(output)], check=True)
        return output

    def library(name, versions):
        script = root / (name + ".map")
        script.write_text(versions)
        return compile_file(name, "int anchor(void) { return 1; } int foo(void) { return 2; }", [
            "-shared", "-fPIC", "-nostdlib", "-Wl,-soname,libc.so.6", "-Wl,--version-script=" + str(script),
        ])

    host = library("host-libc.so.6", "GLIBC_2.2.5 { global: anchor; foo; local: *; };")
    newer = library("new-libc.so.6", "GLIBC_2.2.5 { global: anchor; local: *; }; GLIBC_9.0 { global: foo; };")
    target = compile_file("target.so", "extern int anchor(void); extern int foo(void); int entry(void) { return anchor()+foo(); }", [
        "-shared", "-fPIC", "-nostdlib", str(newer),
    ])
    module = compile_file("module.ko", '''
const char info[] __attribute__((section(".modinfo"))) = "vermagic=old-version";
struct version { unsigned long crc; char name[64 - sizeof(unsigned long)]; };
const struct version versions[] __attribute__((section("__versions"))) = {{1, "demo_symbol"}};
''', ["-c"])
    symvers = root / "Module.symvers"
    symvers.write_text("0x12345678\tdemo_symbol\tvmlinux\tEXPORT_SYMBOL\n")
    shim = compile_file("io_shim.so", '''
#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
int msync(void *addr, size_t len, int flags) {
    if (getenv("CADAPT_TEST_MSYNC_FAIL")) {
        FILE *marker = fopen(getenv("CADAPT_TEST_MSYNC_MARKER"), "w");
        if (marker) { fprintf(marker, "%zu", len); fclose(marker); }
        errno = EIO; return -1;
    }
    int (*real_msync)(void *, size_t, int) = dlsym(RTLD_NEXT, "msync");
    return real_msync(addr, len, flags);
}
DIR *opendir(const char *path) {
    const char *fail = getenv("CADAPT_TEST_SCAN_FAIL");
    if (fail && strcmp(path, fail) == 0) { errno = EACCES; return NULL; }
    DIR *(*real_opendir)(const char *) = dlsym(RTLD_NEXT, "opendir");
    return real_opendir(path);
}
int open(const char *path, int flags, ...) {
    const char *fail = getenv("CADAPT_TEST_READ_FAIL");
    if (fail && strcmp(path, fail) == 0) { errno = EACCES; return -1; }
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args; va_start(args, flags); mode = va_arg(args, mode_t); va_end(args);
    }
    int (*real_open)(const char *, int, ...) = dlsym(RTLD_NEXT, "open");
    return real_open(path, flags, mode);
}
int __open_2(const char *path, int flags) { return open(path, flags); }
''', ["-shared", "-fPIC", "-ldl"])

    def run(label, args, **faults):
        marker = root / (label + ".msync")
        faults["CADAPT_TEST_MSYNC_MARKER"] = str(marker)
        file = pathlib.Path(args[args.index("-t") + 1] if "-t" in args else args[args.index("-m") + 1])
        before = file.read_bytes() if file.is_file() else None
        environment = dict(os.environ, LD_PRELOAD=str(shim), **faults)
        result = subprocess.run([str(binary), *args], env=environment, capture_output=True, text=True, timeout=60)
        assert result.returncode == 1, (label, result.stdout, result.stderr)
        assert "适配完成" not in result.stdout and "适配成功" not in result.stdout, label
        if "CADAPT_TEST_MSYNC_FAIL" in faults:
            assert marker.exists(), "test never reached msync: " + result.stdout + result.stderr
            assert int(marker.read_text()) == file.stat().st_size
            assert file.read_bytes() != before, "test failed before the intended write"
        print("PASS:", label)

    run("glibc-msync-failure", ["-c", str(host), "-t", str(target)], CADAPT_TEST_MSYNC_FAIL="1")
    run("kmod-msync-failure", ["-m", str(module), "-s", str(symvers), "-v", "new"], CADAPT_TEST_MSYNC_FAIL="1")
    scan = root / "scan"
    scan.mkdir()
    inaccessible = scan / "inaccessible"
    inaccessible.mkdir()
    target = scan / "target.so"
    # 使用原始目标，确保目录检查失败时尚未开始修改已收集的文件。
    original = compile_file("original.so", "extern int anchor(void); extern int foo(void); int entry(void) { return anchor()+foo(); }", [
        "-shared", "-fPIC", "-nostdlib", str(newer),
    ])
    shutil.copy2(original, target)
    before = target.read_bytes()
    run("directory-scan-failure", ["-c", str(host), "-t", str(scan)], CADAPT_TEST_SCAN_FAIL=str(inaccessible))
    assert target.read_bytes() == before
    run("directory-file-read-failure", ["-c", str(host), "-t", str(scan)], CADAPT_TEST_READ_FAIL=str(target))
    assert target.read_bytes() == before
