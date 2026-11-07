"""Run with: python3 tests/elf32_regression.py /path/to/cadapt

32 位 ELF 回归：解释器、RPATH、GLIBC、内核模块，全部用真实的
-m32 产物验证（解析/改写/校验走与 64 位同一份模板代码）。

注意：本机内核可能无法执行 32 位程序（无 IA32 兼容），因此本套件
不用“运行改写后的程序”验证，改用 readelf 与 cadapt 自身的
-p/--print-rpath 输出、以及字节级断言验证结果。
"""

import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

binary = pathlib.Path(sys.argv[1]).resolve()


def have_m32():
    with tempfile.TemporaryDirectory() as probe_dir:
        probe = pathlib.Path(probe_dir)
        source = probe / "probe.c"
        source.write_text("int main(void) { return 0; }\n")
        result = subprocess.run(
            ["cc", "-m32", "-c", str(source), "-o", str(probe / "probe.o")],
            capture_output=True)
        return result.returncode == 0


if not have_m32():
    print("SKIP: cc -m32 is not available")
    sys.exit(0)


def run_cadapt(args, succeeds=True):
    result = subprocess.run([str(binary)] + args, capture_output=True,
                            text=True, timeout=60)
    assert (result.returncode == 0) == succeeds, result.stdout + result.stderr
    return result


def readelf(*args):
    result = subprocess.run(["readelf"] + list(args), capture_output=True,
                            text=True, timeout=20)
    assert result.returncode == 0, result.stderr
    return result.stdout


# ---- 32 位 ELF 解析（验证用，与工具实现无关） ----

def elf32_sections(data):
    shoff = struct.unpack_from("<I", data, 32)[0]
    entsize, count, names_index = struct.unpack_from("<HHH", data, 46)
    headers = [struct.unpack_from("<IIIIIIIIII", data, shoff + i * entsize)
               for i in range(count)]
    names = headers[names_index]
    result = {}
    for header in headers:
        start = names[4] + header[0]
        name = data[start:data.index(0, start)].decode()
        result[name] = header
    return result


def elf32_interp(data):
    phoff = struct.unpack_from("<I", data, 28)[0]
    entsize, count = struct.unpack_from("<HH", data, 42)
    for i in range(count):
        p_type, p_offset, _, _, p_filesz = struct.unpack_from(
            "<IIIII", data, phoff + i * entsize)
        if p_type == 3:  # PT_INTERP
            raw = data[p_offset:p_offset + p_filesz]
            return raw.split(b"\0")[0].decode()
    return None


def elf32_runpath(data):
    phoff = struct.unpack_from("<I", data, 28)[0]
    entsize, count = struct.unpack_from("<HH", data, 42)
    dynamic = None
    for i in range(count):
        p_type, p_offset, _, _, p_filesz = struct.unpack_from(
            "<IIIII", data, phoff + i * entsize)
        if p_type == 2:  # PT_DYNAMIC
            dynamic = (p_offset, p_filesz)
    assert dynamic is not None, "no PT_DYNAMIC"
    tables = elf32_sections(data)
    dynstr = tables[".dynstr"]
    offset, size = dynamic
    value = None
    for j in range(0, size, 8):
        tag, val = struct.unpack_from("<iI", data, offset + j)
        if tag == 29:  # DT_RUNPATH
            start = dynstr[4] + val
            value = data[start:data.index(0, start)].decode()
        elif tag == 0:
            break
    return value


def elf32_dynamic_value(data, tag):
    """Return the d_val/d_ptr of the first .dynamic entry with the tag."""
    phoff = struct.unpack_from("<I", data, 28)[0]
    entsize, count = struct.unpack_from("<HH", data, 42)
    dynamic = None
    for i in range(count):
        p_type, p_offset, _, _, p_filesz = struct.unpack_from(
            "<IIIII", data, phoff + i * entsize)
        if p_type == 2:  # PT_DYNAMIC
            dynamic = (p_offset, p_filesz)
    assert dynamic is not None, "no PT_DYNAMIC"
    offset, size = dynamic
    for j in range(0, size, 8):
        entry_tag, val = struct.unpack_from("<iI", data, offset + j)
        if entry_tag == 0:
            break
        if entry_tag == tag:
            return val
    return None


def elf32_load_count(data):
    phoff = struct.unpack_from("<I", data, 28)[0]
    entsize, count = struct.unpack_from("<HH", data, 42)
    loads = 0
    for i in range(count):
        p_type = struct.unpack_from("<I", data, phoff + i * entsize)[0]
        if p_type == 1:  # PT_LOAD
            loads += 1
    return loads


def elf32_verneed_names(data):
    """返回目标 .gnu.version_r 中 libc.so.6 的版本名列表。"""
    tables = elf32_sections(data)
    strings = tables[".dynstr"][4]
    offset = tables[".gnu.version_r"][4]
    while True:
        _, count, file, aux, next_record = struct.unpack_from(
            "<HHIII", data, offset)
        start = strings + file
        if data[start:data.index(0, start)] == b"libc.so.6":
            names = []
            node = offset + aux
            for i in range(count):
                record = struct.unpack_from("<IHHII", data, node)
                start = strings + record[3]
                names.append(data[start:data.index(0, start)].decode())
                node += record[4]
            return names
        assert next_record, "missing libc dependency"
        offset += next_record


with tempfile.TemporaryDirectory(prefix="cadapt-elf32-test-") as directory:
    root = pathlib.Path(directory)

    # ---- 32 位固件 ----
    start_c = root / "start32.c"
    start_c.write_text(
        'void _start(void) { __asm__ volatile("mov $1,%eax; xor %ebx,%ebx; int $0x80"); }\n')
    loader32_c = root / "loader32.c"
    loader32_c.write_text("int loader_anchor(void) { return 1; }\n")
    loader32 = root / "ld32.so"
    subprocess.run(["cc", "-m32", "-shared", "-nostdlib", "-fPIC",
                    str(loader32_c), "-o", str(loader32)], check=True)
    loader64 = root / "ld64.so"
    subprocess.run(["cc", "-shared", "-nostdlib", "-fPIC",
                    str(loader32_c), "-o", str(loader64)], check=True)

    orig_interp = root / "a-very-long-original-interp-path.so"
    hello32 = root / "hello32"
    subprocess.run(["cc", "-m32", "-nostdlib", str(start_c),
                    "-Wl,-dynamic-linker," + str(orig_interp),
                    "-Wl,-rpath,/opt/orig-long-path",
                    "-o", str(hello32)], check=True)
    hello64 = root / "hello64"
    subprocess.run(["cc", "-nostdlib", str(start_c),
                    "-Wl,-dynamic-linker,/lib64/ld-linux-x86-64.so.2",
                    "-o", str(hello64)], check=True)

    # ---- 解释器：32 位 ----
    assert elf32_interp(hello32.read_bytes()) == str(orig_interp)
    result = run_cadapt(["-p", "-t", str(hello32)])
    assert str(orig_interp) in result.stdout, result.stdout

    # 更短：原地改写。
    target = root / "interp-short"
    shutil.copy2(hello32, target)
    run_cadapt(["-i", str(loader32), "-t", str(target)])
    assert elf32_interp(target.read_bytes()) == str(loader32)
    result = run_cadapt(["-p", "-t", str(target)])
    assert str(loader32) in result.stdout

    # 更长：重写文件（新串追加到末尾）。
    target = root / "interp-long"
    shutil.copy2(hello32, target)
    before_size = target.stat().st_size
    longer = root / "an-even-longer-new-interp-path-than-the-original.so"
    shutil.copy2(loader32, longer)  # 新解释器必须是真实存在的 32 位 ET_DYN 文件。
    run_cadapt(["-i", str(longer), "-t", str(target)])
    data = target.read_bytes()
    assert elf32_interp(data) == str(longer)
    assert len(data) == before_size + len(str(longer)) + 1
    assert elf32_sections(data)[".interp"][4] + elf32_sections(data)[".interp"][5] == len(data)
    result = run_cadapt(["-p", "-t", str(target)])
    assert str(longer) in result.stdout
    assert "INTERP" in readelf("-l", str(target))

    # 试运行不改文件。
    target = root / "interp-dry"
    shutil.copy2(hello32, target)
    before = target.read_bytes()
    run_cadapt(["-i", str(loader32), "-t", str(target), "--dry-run"])
    assert target.read_bytes() == before

    # 只给 64 位解释器：不匹配，警告并跳过，文件不动。
    target = root / "interp-mismatch"
    shutil.copy2(hello32, target)
    before = target.read_bytes()
    result = run_cadapt(["-i", str(loader64), "-t", str(target)], succeeds=False)
    assert "没有与目标匹配的解释器" in result.stderr, result.stderr
    assert "32 位小端" in result.stderr
    assert target.read_bytes() == before

    # 两个都给（64 位在前）：挑中匹配的 32 位。
    target = root / "interp-both"
    shutil.copy2(hello32, target)
    run_cadapt(["-i", str(loader64), "-i", str(loader32), "-t", str(target)])
    assert elf32_interp(target.read_bytes()) == str(loader32)

    # 大端文件：警告并跳过，文件不动。
    target = root / "interp-bigendian"
    data = bytearray(hello32.read_bytes())
    data[5] = 2  # EI_DATA = ELFDATA2MSB
    target.write_bytes(data)
    before = target.read_bytes()
    result = run_cadapt(["-i", str(loader32), "-t", str(target)], succeeds=False)
    assert "大端" in result.stderr, result.stderr
    assert target.read_bytes() == before

    # ---- RPATH：32 位 ----
    assert elf32_runpath(hello32.read_bytes()) == "/opt/orig-long-path"
    result = run_cadapt(["--print-rpath", "-t", str(hello32)])
    assert "/opt/orig-long-path" in result.stdout

    target = root / "rpath-short"
    shutil.copy2(hello32, target)
    run_cadapt(["-r", "/opt/short", "-t", str(target)])
    assert elf32_runpath(target.read_bytes()) == "/opt/short"

    target = root / "rpath-clear"
    shutil.copy2(hello32, target)
    run_cadapt(["-r", "", "-t", str(target)])
    assert elf32_runpath(target.read_bytes()) == ""

    target = root / "rpath-long"
    shutil.copy2(hello32, target)
    before = target.read_bytes()
    before_loads = elf32_load_count(before)
    run_cadapt(["-r", "/opt/a-much-longer-runpath-than-before", "-t", str(target)])
    data = target.read_bytes()
    assert len(data) > len(before), "rewrite did not grow the file"
    assert elf32_runpath(data) == "/opt/a-much-longer-runpath-than-before"
    # DT_STRTAB 指向被搬走的 .dynstr，且新增了一个 PT_LOAD 段。
    assert elf32_dynamic_value(data, 5) == elf32_sections(data)[".dynstr"][3]
    assert elf32_load_count(data) == before_loads + 1
    result = run_cadapt(["--print-rpath", "-t", str(target)])
    assert "/opt/a-much-longer-runpath-than-before" in result.stdout

    # 无 RPATH 的 32 位目标：新增 DT_RUNPATH（.dynamic 增长一个槽位）。
    plain32 = root / "hello32-plain"
    subprocess.run(["cc", "-m32", "-nostdlib", str(start_c),
                    "-Wl,-dynamic-linker," + str(orig_interp),
                    "-o", str(plain32)], check=True)
    assert elf32_runpath(plain32.read_bytes()) is None
    before = plain32.read_bytes()
    run_cadapt(["-r", "/opt/new-rpath-32", "-t", str(plain32)])
    data = plain32.read_bytes()
    assert len(data) > len(before), "rewrite did not grow the file"
    assert elf32_runpath(data) == "/opt/new-rpath-32"
    assert elf32_dynamic_value(data, 5) == elf32_sections(data)[".dynstr"][3]
    assert elf32_load_count(data) == elf32_load_count(before) + 1

    # ---- GLIBC：32 位 ----
    def build_library32(name, source, versions):
        folder = root / name
        folder.mkdir()
        code = folder / "library.c"
        script = folder / "versions.map"
        library = folder / "libc.so.6"
        code.write_text(source)
        script.write_text(versions)
        subprocess.run([
            "cc", "-m32", "-shared", "-fPIC", "-nostdlib", str(code),
            "-Wl,-soname,libc.so.6", "-Wl,--version-script=" + str(script),
            "-o", str(library),
        ], check=True)
        return library

    host32 = build_library32(
        "host32", "int anchor(void) { return 1; } int foo(void) { return 2; }",
        "GLIBC_2.2.5 { global: anchor; foo; local: *; };",
    )
    newer32 = build_library32(
        "newer32", "int anchor(void) { return 1; } int foo(void) { return 2; } int absent(void) { return 3; }",
        "GLIBC_2.2.5 { global: anchor; local: *; }; GLIBC_9.0 { global: foo; absent; };",
    )
    host64 = root / "host64-libc.so.6"
    shutil.copy2(pathlib.Path("/lib/x86_64-linux-gnu/libc.so.6"), host64)

    def target32(name, symbols):
        code = root / (name + ".c")
        output = root / (name + ".so")
        code.write_text(
            "\n".join("extern int " + symbol + "(void);" for symbol in symbols)
            + "\nint entry(void) { return " + "+".join(symbol + "()" for symbol in symbols) + "; }"
        )
        subprocess.run([
            "cc", "-m32", "-shared", "-fPIC", "-nostdlib", str(code),
            str(newer32), "-o", str(output),
        ], check=True)
        return output

    missing32 = target32("missing-symbol32", ["anchor", "foo", "absent"])
    before = missing32.read_bytes()
    run_cadapt(["-c", str(host32), "-t", str(missing32)], succeeds=False)
    assert missing32.read_bytes() == before

    valid32 = target32("valid32", ["anchor", "foo"])
    run_cadapt(["-c", str(host32), "-t", str(valid32)])
    assert elf32_verneed_names(valid32.read_bytes()) == ["GLIBC_2.2.5"]

    # 只给 64 位 libc：不匹配，警告并跳过，文件不动。
    target = root / "glibc-mismatch32.so"
    shutil.copy2(valid32, target)
    before = target.read_bytes()
    result = run_cadapt(["-c", str(host64), "-t", str(target)], succeeds=False)
    assert "没有与目标匹配的 C 库" in result.stderr, result.stderr
    assert "32 位小端" in result.stderr
    assert target.read_bytes() == before

    # 混合目录：32 位与 64 位目标各自匹配对应的 libc。
    mixed = root / "mixed-glibc"
    mixed.mkdir()
    # 64 位 newer/host 与 64 位目标：沿用 64 位回归同一模式现建。
    newer64 = root / "newer64-libc.so.6"
    subprocess.run([
        "cc", "-shared", "-fPIC", "-nostdlib", str(root / "newer32" / "library.c"),
        "-Wl,-soname,libc.so.6",
        "-Wl,--version-script=" + str(root / "newer32" / "versions.map"),
        "-o", str(newer64),
    ], check=True)
    host64lib = root / "host64b-libc.so.6"
    subprocess.run([
        "cc", "-shared", "-fPIC", "-nostdlib", str(root / "host32" / "library.c"),
        "-Wl,-soname,libc.so.6",
        "-Wl,--version-script=" + str(root / "host32" / "versions.map"),
        "-o", str(host64lib),
    ], check=True)
    code64 = root / "valid64.c"
    code64.write_text(
        "extern int anchor(void); extern int foo(void);\n"
        "int entry(void) { return anchor() + foo(); }\n")
    target64 = root / "valid64.so"
    subprocess.run([
        "cc", "-shared", "-fPIC", "-nostdlib", str(code64), str(newer64),
        "-o", str(target64),
    ], check=True)
    shutil.copy2(target32("valid32-mixed", ["anchor", "foo"]), mixed / "valid32.so")
    shutil.copy2(target64, mixed / "valid64.so")
    run_cadapt(["-c", str(host32), "-c", str(host64lib), "-t", str(mixed)])
    assert elf32_verneed_names((mixed / "valid32.so").read_bytes()) == ["GLIBC_2.2.5"]
    # 64 位目标同样只剩 GLIBC_2.2.5（用 64 位布局复核）。
    data64 = (mixed / "valid64.so").read_bytes()
    shoff = struct.unpack_from("<Q", data64, 40)[0]
    entsize, count, names_index = struct.unpack_from("<HHH", data64, 58)
    headers = [struct.unpack_from("<IIQQQQIIQQ", data64, shoff + i * entsize)
               for i in range(count)]
    names = headers[names_index]
    tables64 = {}
    for header in headers:
        start = names[4] + header[0]
        tables64[data64[start:data64.index(0, start)].decode()] = header
    strings = tables64[".dynstr"][4]
    offset = tables64[".gnu.version_r"][4]
    _, ver_count, ver_file, ver_aux, _ = struct.unpack_from("<HHIII", data64, offset)
    assert ver_count == 1
    node = offset + ver_aux
    record = struct.unpack_from("<IHHII", data64, node)
    start = strings + record[3]
    assert data64[start:data64.index(0, start)] == b"GLIBC_2.2.5"

    # ---- 混合目录：解释器（32 位 + 64 位 + 大端 + 无 PT_INTERP） ----
    mixed_interp = root / "mixed-interp"
    mixed_interp.mkdir()
    shutil.copy2(hello32, mixed_interp / "hello32")
    shutil.copy2(hello64, mixed_interp / "hello64")
    be_file = bytearray(hello32.read_bytes())
    be_file[5] = 2
    (mixed_interp / "bigendian").write_bytes(be_file)
    shutil.copy2(loader32, mixed_interp / "lib-no-interp.so")
    result = run_cadapt(["-i", str(loader64), "-i", str(loader32), "-t", str(mixed_interp)])
    assert "大端" in result.stderr, result.stderr
    assert "跳过（无 PT_INTERP）" in result.stdout
    assert elf32_interp((mixed_interp / "hello32").read_bytes()) == str(loader32)
    assert "ld64.so" in run_cadapt(["-p", "-t", str(mixed_interp / "hello64")]).stdout

    # ---- 内核模块：32 位 ----
    symvers = root / "Module32.symvers"
    symvers.write_text("0x12345678\tdemo_symbol\tvmlinux\tEXPORT_SYMBOL\n")

    def module32(name, source):
        code = root / (name + ".c")
        target = root / (name + ".ko")
        code.write_text(source)
        subprocess.run(["cc", "-m32", "-c", str(code), "-o", str(target)], check=True)
        return target

    modinfo = 'const char info[] __attribute__((section(".modinfo"))) = "vermagic=old-version";\n'
    versions32 = '''
struct version { unsigned long crc; char name[64 - sizeof(unsigned long)]; };
const struct version versions[] __attribute__((section("__versions"))) = {{1, "demo_symbol"}};
'''
    ko32 = module32("valid32", modinfo + versions32)
    # 32 位 __versions 条目也是 64 字节（4 字节 CRC + 60 字节名），
    # 与内核的 struct modversion_info 一致。
    assert elf32_sections(ko32.read_bytes())["__versions"][5] == 64
    run_cadapt(["-m", str(ko32), "-s", str(symvers), "-v", "new"])
    data = ko32.read_bytes()
    assert b"vermagic=new\0" in data
    assert struct.pack("<I", 0x12345678) + b"demo_symbol\0" in data

    # 目标模块架构与当前系统不一致、又用系统来源时：警告并拒绝，文件不动。
    ko_arm = module32("arm32", modinfo + versions32)
    data = bytearray(ko_arm.read_bytes())
    struct.pack_into("<H", data, 18, 183)  # e_machine = EM_AARCH64
    ko_arm.write_bytes(data)
    before = ko_arm.read_bytes()
    result = run_cadapt(["-m", str(ko_arm)], succeeds=False)
    assert "目标模块架构" in result.stderr, result.stderr
    assert "与当前系统" in result.stderr
    assert ko_arm.read_bytes() == before
    # 同一文件显式提供 -s 与 -v 时不走系统来源，可以改。
    run_cadapt(["-m", str(ko_arm), "-s", str(symvers), "-v", "new"])
    assert b"vermagic=new\0" in ko_arm.read_bytes()

    # 大端模块：警告并拒绝，文件不动。
    ko_be = module32("bigendian32", modinfo + versions32)
    data = bytearray(ko_be.read_bytes())
    data[5] = 2
    ko_be.write_bytes(data)
    before = ko_be.read_bytes()
    result = run_cadapt(["-m", str(ko_be), "-s", str(symvers), "-v", "new"],
                        succeeds=False)
    assert "大端" in result.stderr, result.stderr
    assert ko_be.read_bytes() == before

    print("PASS: elf32")
