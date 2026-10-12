"""Run with: python3 tests/kmod_regression.py /path/to/cadapt"""
import pathlib
import os
import struct
import subprocess
import sys
import tempfile


binary = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="cadapt-kmod-test-") as directory:
    root = pathlib.Path(directory)
    symvers = root / "Module.symvers"
    symvers.write_text("0x12345678\tdemo_symbol\tvmlinux\tEXPORT_SYMBOL\n")

    def module(name, source):
        code = root / (name + ".c")
        target = root / (name + ".ko")
        code.write_text(source)
        subprocess.run(["cc", "-c", str(code), "-o", str(target)], check=True)
        return target

    def run(target, succeeds, vermagic="new", environment=None, symvers_file=None):
        before = target.read_bytes() if target.exists() else None
        args = [str(binary), "-m", str(target), "-s", str(symvers_file or symvers)]
        if vermagic is not None:
            args += ["-v", vermagic]
        result = subprocess.run(
            args, env=environment, capture_output=True, text=True, timeout=60,
        )
        assert (result.returncode == 0) == succeeds, result.stdout + result.stderr
        assert ("内核模块适配成功" in result.stdout) == succeeds, result.stdout
        if not succeeds and before is not None:
            assert target.read_bytes() == before, "failed validation modified target"
            assert "modify symbol" not in result.stdout and "modify ver str magic" not in result.stdout
        print("PASS:", target.name)

    modinfo = 'const char info[] __attribute__((section(".modinfo"))) = "vermagic=old-version";\n'
    versions = '''
struct version { unsigned long crc; char name[64 - sizeof(unsigned long)]; };
const struct version versions[] __attribute__((section("__versions"))) = {{1, "demo_symbol"}};
'''
    target = module("valid", modinfo + versions)
    run(target, True)
    data = target.read_bytes()
    assert b"vermagic=new\0" in data
    assert struct.pack("@L", 0x12345678) + b"demo_symbol\0" in data

    target = module("without-versions", modinfo)
    run(target, True)
    assert b"vermagic=new\0" in target.read_bytes()

    run(root / "missing.ko", False)
    run(module("without-modinfo", "int demo;\n"), False)
    run(module("without-vermagic", 'const char info[] __attribute__((section(".modinfo"))) = "license=GPL";\n'), False)
    run(module("invalid-versions", modinfo + 'const char versions[] __attribute__((section("__versions"))) = "bad";\n'), False)

    partial_versions = '''
struct version { unsigned long crc; char name[64 - sizeof(unsigned long)]; };
const struct version versions[] __attribute__((section("__versions"))) = {
    {1, "demo_symbol"}, {2, "zzz_cadapt_missing_symbol"}
};
'''
    # 已找到的符号排序在缺失符号之前，检查失败时也不能留下先前的CRC修改。
    run(module("missing-symbol", modinfo + partial_versions), False)
    run(module("long-vermagic", modinfo + versions), False, vermagic="a-version-longer-than-old-version")

    # 隔离宿主内核版本，确保“自动获取不到vermagic”测试不依赖本机模块安装情况。
    shim_source = root / "uname_shim.c"
    shim_source.write_text('''
#include <sys/utsname.h>
#include <string.h>
int uname(struct utsname *info) {
    memset(info, 0, sizeof(*info));
    strcpy(info->sysname, "Linux");
    strcpy(info->release, "cadapt-test-no-such-kernel");
    return 0;
}
''')
    shim = root / "uname_shim.so"
    subprocess.run(["cc", "-shared", "-fPIC", str(shim_source), "-o", str(shim)], check=True)
    environment = dict(os.environ, LD_PRELOAD=str(shim))
    run(module("empty-auto-vermagic", modinfo + versions), False, vermagic=None, environment=environment)
    target = module("explicit-vermagic-without-system", modinfo + versions)
    run(target, True, environment=environment)
    assert b"vermagic=new\0" in target.read_bytes()
    target = module("exact-length-vermagic", modinfo + versions)
    run(target, True, vermagic="new-version")
    assert b"vermagic=new-version\0" in target.read_bytes()
    target = module("vermagic-prefix", '''
const char info[] __attribute__((section(".modinfo"))) =
    "description=vermagic=leave-this-alone\\0vermagic=old-version";
''' + versions)
    run(target, True)
    assert b"description=vermagic=leave-this-alone\0vermagic=new\0" in target.read_bytes()

    for label, row, crc in [
        ("namespace", "0x12345678\tdemo_symbol\tvmlinux\tEXPORT_SYMBOL\tDEMO\n", 0x12345678),
        ("spaces", "ABCD demo_symbol vmlinux EXPORT_SYMBOL\n", 0xABCD),
        ("zero-crc", "0x00000000\tdemo_symbol\tvmlinux\tEXPORT_SYMBOL\n", 0),
        ("max-crc", "0xffffffff\tdemo_symbol\tvmlinux\tEXPORT_SYMBOL\n", 0xFFFFFFFF),
    ]:
        source = root / (label + ".symvers")
        source.write_text(row)
        target = module(label, modinfo + versions)
        run(target, True, symvers_file=source)
        assert struct.pack("@L", crc) + b"demo_symbol\0" in target.read_bytes()

    for label, row in [
        ("invalid-crc", "xyz"), ("signed-crc", "-1"), ("crc-trailing-junk", "0x12xyz"),
        ("overflow-crc", "0x100000000"), ("empty-hex-crc", "0x"),
    ]:
        source = root / (label + ".symvers")
        source.write_text(row + "\tdemo_symbol\tvmlinux\tEXPORT_SYMBOL\n")
        run(module(label, modinfo + versions), False, symvers_file=source)
    for label, row in [
        ("too-few-columns", "0x12345678\tdemo_symbol\tvmlinux\n"),
        ("too-many-columns", "0x12345678\tdemo_symbol\tvmlinux\tEXPORT_SYMBOL\tDEMO\textra\n"),
        ("valid-then-invalid", symvers.read_text() + "broken\n"),
        ("conflicting-crc", symvers.read_text() + "0x1\tdemo_symbol\tvmlinux\tEXPORT_SYMBOL\n"),
    ]:
        source = root / (label + ".symvers")
        source.write_text(row)
        run(module(label, modinfo + versions), False, symvers_file=source)
    run(module("missing-symvers", modinfo + versions), False, symvers_file=root / "missing.symvers")
