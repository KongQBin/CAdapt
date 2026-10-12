"""Deterministic system-module tests: python3 tests/system_modules_regression.py /path/to/cadapt"""
import os
import pathlib
import struct
import subprocess
import sys
import tempfile


binary = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="cadapt-system-test-") as directory:
    root = pathlib.Path(directory)
    system = root / "system"
    system.mkdir()

    def module(path, crc=1, vermagic="old-version"):
        source = path.with_suffix(".c")
        source.write_text('''
const char info[] __attribute__((section(".modinfo"))) = "vermagic=%s";
struct version { unsigned long crc; char name[64 - sizeof(unsigned long)]; };
const struct version versions[] __attribute__((section("__versions"))) = {{%d, "demo_symbol"}};
''' % (vermagic, crc))
        subprocess.run(["cc", "-c", str(source), "-o", str(path)], check=True)
        return path

    module(system / "a.ko", crc=0x12345678)
    (system / "modules.dep").write_text("a.ko:\n")
    source = root / "system_shim.c"
    source.write_text('''
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
static const char *mapped(const char *path, char *buffer, size_t size) {
    const char *marker = "/modules/cadapt-regression-kernel/";
    const char *match = strstr(path, marker);
    if (!match) return path;
    snprintf(buffer, size, "%s/%s", getenv("CADAPT_TEST_MODULES"), match + strlen(marker));
    return buffer;
}
int uname(struct utsname *info) {
    memset(info, 0, sizeof(*info));
    strcpy(info->sysname, "Linux"); strcpy(info->release, "cadapt-regression-kernel");
    return 0;
}
int access(const char *path, int mode) {
    char buffer[4096]; int (*real_access)(const char *, int) = dlsym(RTLD_NEXT, "access");
    return real_access(mapped(path, buffer, sizeof(buffer)), mode);
}
FILE *fopen64(const char *path, const char *mode) {
    char buffer[4096]; FILE *(*real_fopen)(const char *, const char *) = dlsym(RTLD_NEXT, "fopen64");
    return real_fopen(mapped(path, buffer, sizeof(buffer)), mode);
}
FILE *fopen(const char *path, const char *mode) {
    char buffer[4096]; FILE *(*real_fopen)(const char *, const char *) = dlsym(RTLD_NEXT, "fopen");
    return real_fopen(mapped(path, buffer, sizeof(buffer)), mode);
}
int open(const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list args; va_start(args, flags); mode = va_arg(args, mode_t); va_end(args); }
    char buffer[4096]; int (*real_open)(const char *, int, ...) = dlsym(RTLD_NEXT, "open");
    return real_open(mapped(path, buffer, sizeof(buffer)), flags, mode);
}
int open64(const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list args; va_start(args, flags); mode = va_arg(args, mode_t); va_end(args); }
    char buffer[4096]; int (*real_open)(const char *, int, ...) = dlsym(RTLD_NEXT, "open64");
    return real_open(mapped(path, buffer, sizeof(buffer)), flags, mode);
}
''')
    shim = root / "system_shim.so"
    subprocess.run(["cc", "-shared", "-fPIC", str(source), "-o", str(shim), "-ldl"], check=True)
    environment = dict(os.environ, LD_PRELOAD=str(shim), CADAPT_TEST_MODULES=str(system))

    def run(label, options, success):
        target = module(root / (label + ".ko"))
        before = target.read_bytes()
        result = subprocess.run([str(binary), "-m", str(target), *options], env=environment,
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == (0 if success else 1), result.stdout + result.stderr
        if not success:
            assert target.read_bytes() == before, "failed source validation changed file"
            assert "内核模块适配成功" not in result.stdout
        print("PASS:", label)
        return target

    empty = root / "empty.symvers"
    empty.write_text("")
    run("explicit-source-no-fallback", ["-s", str(empty), "-v", "foreign"], False)
    run("foreign-version-no-source", ["-v", "foreign"], False)
    target = run("automatic-system-source", [], True)
    assert struct.pack("@L", 0x12345678) + b"demo_symbol\0" in target.read_bytes()
    target = run("matching-system-version", ["-v", "old-version"], True)
    assert struct.pack("@L", 0x12345678) + b"demo_symbol\0" in target.read_bytes()
    explicit = root / "explicit.symvers"
    explicit.write_text("0x3456\tdemo_symbol\tvmlinux\tEXPORT_SYMBOL\n")
    target = run("explicit-source-with-version", ["-s", str(explicit), "-v", "foreign"], True)
    assert struct.pack("@L", 0x3456) + b"demo_symbol\0" in target.read_bytes()
    assert b"vermagic=foreign\0" in target.read_bytes()
