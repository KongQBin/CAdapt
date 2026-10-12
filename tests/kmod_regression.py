"""Run with: python3 tests/kmod_regression.py /path/to/cadapt"""
import pathlib
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

    def run(target, succeeds):
        before = target.read_bytes() if target.exists() else None
        result = subprocess.run(
            [str(binary), "-m", str(target), "-s", str(symvers), "-v", "new"],
            capture_output=True, text=True, timeout=60,
        )
        assert (result.returncode == 0) == succeeds, result.stdout + result.stderr
        assert ("内核模块适配成功" in result.stdout) == succeeds, result.stdout
        if not succeeds and before is not None:
            assert target.read_bytes() == before, "failed initialization modified target"
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
