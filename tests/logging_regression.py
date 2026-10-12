"""Run with: python3 tests/logging_regression.py /path/to/cadapt"""
import os
import pathlib
import subprocess
import sys
import tempfile


binary = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="cadapt-log-test-") as directory:
    root = pathlib.Path(directory)
    source = root / "log_shim.c"
    source.write_text('''
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
FILE *fopen(const char *path, const char *mode) {
    if (getenv("CADAPT_TEST_LOG_OPEN_FAIL") && strstr(path, "Errlog.txt")) { errno = EACCES; return NULL; }
    FILE *(*real_fopen)(const char *, const char *) = dlsym(RTLD_NEXT, "fopen");
    return real_fopen(path, mode);
}
FILE *fopen64(const char *path, const char *mode) {
    if (getenv("CADAPT_TEST_LOG_OPEN_FAIL") && strstr(path, "Errlog.txt")) { errno = EACCES; return NULL; }
    FILE *(*real_fopen)(const char *, const char *) = dlsym(RTLD_NEXT, "fopen64");
    return real_fopen(path, mode);
}
ssize_t readlink(const char *path, char *buffer, size_t size) {
    if (getenv("CADAPT_TEST_LONG_EXE")) { memset(buffer, 'X', size); return size; }
    ssize_t (*real_readlink)(const char *, char *, size_t) = dlsym(RTLD_NEXT, "readlink");
    return real_readlink(path, buffer, size);
}
''')
    shim = root / "log_shim.so"
    subprocess.run(["cc", "-shared", "-fPIC", str(source), "-o", str(shim), "-ldl"], check=True)
    for flag in ["CADAPT_TEST_LOG_OPEN_FAIL", "CADAPT_TEST_LONG_EXE"]:
        environment = dict(os.environ, LD_PRELOAD=str(shim), **{flag: "1"})
        result = subprocess.run([str(binary), "-c", str(root / "missing-libc.so.6"), "-t", "missing"],
                                capture_output=True, text=True, env=environment, timeout=10)
        assert result.returncode == 1, result.stdout + result.stderr
        assert "stderr" in result.stderr and "打开文件失败" in result.stderr, result.stderr
        assert "missing-libc.so.6" in result.stderr
        print("PASS:", flag)
