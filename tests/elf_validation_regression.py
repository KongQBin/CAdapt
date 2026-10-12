"""Run with: python3 tests/elf_validation_regression.py /path/to/cadapt"""
import pathlib
import struct
import subprocess
import sys
import tempfile


binary = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="cadapt-elf-test-") as directory:
    root = pathlib.Path(directory)

    def compile_file(name, source, flags):
        code = root / (name + ".c")
        output = root / name
        code.write_text(source)
        subprocess.run(["cc", *flags, str(code), "-o", str(output)], check=True)
        return output

    versions = root / "versions.map"
    versions.write_text("GLIBC_2.2.5 { global: anchor; local: *; }; GLIBC_2.3 {} GLIBC_2.2.5;")
    host = compile_file("libc.so.6", "int anchor(void) { return 1; }", [
        "-shared", "-fPIC", "-nostdlib", "-Wl,-soname,libc.so.6", "-Wl,--version-script=" + str(versions),
    ])
    target = compile_file("target.so", "extern int anchor(void); int entry(void) { return anchor(); }", [
        "-shared", "-fPIC", "-nostdlib", str(host),
    ])
    module = compile_file("module.ko", '''
const char info[] __attribute__((section(".modinfo"))) = "vermagic=old-version";
struct version { unsigned long crc; char name[64 - sizeof(unsigned long)]; };
const struct version versions[] __attribute__((section("__versions"))) = {{1, "anchor"}};
char large_bss[1048576];
''', ["-c"])
    symvers = root / "Module.symvers"
    symvers.write_text("0x12345678\tanchor\tvmlinux\tEXPORT_SYMBOL\n")

    def section(data, wanted):
        shoff = struct.unpack_from("<Q", data, 40)[0]
        entsize, count, names_index = struct.unpack_from("<HHH", data, 58)
        strings = struct.unpack_from("<Q", data, shoff + names_index * entsize + 24)[0]
        for i in range(count):
            header = shoff + i * entsize
            name = strings + struct.unpack_from("<I", data, header)[0]
            if data[name:data.index(0, name)].decode() == wanted:
                return header, struct.unpack_from("<Q", data, header + 24)[0], struct.unpack_from("<Q", data, header + 32)[0]
        raise AssertionError("missing section " + wanted)

    cases = []

    def mutation(label, base, mode, offset, fmt, value):
        data = bytearray(base.read_bytes())
        struct.pack_into(fmt, data, offset, value)
        cases.append((label, mode, data))

    # 同一公共解析器的只读、读写入口都应拒绝这些文件头错误。
    for mode, base in [("host", host), ("target", target), ("kmod", module)]:
        for label, data in [("empty", b""), ("truncated-header", base.read_bytes()[:63]), ("garbage", b"x" * 128)]:
            cases.append((mode + "-" + label, mode, data))
        for label, offset, fmt, value in [
            ("bad-magic", 0, "<I", 0), ("elf32", 4, "B", 1), ("big-endian", 5, "B", 2),
            ("ident-version", 6, "B", 0), ("header-version", 20, "<I", 0),
            ("header-size", 52, "<H", 1), ("section-entry-size", 58, "<H", 1),
            ("no-sections", 60, "<H", 0), ("section-name-index", 62, "<H", 65535),
            ("section-offset-overflow", 40, "<Q", 2**64 - 8), ("unaligned-section-table", 40, "<Q", 65),
        ]:
            mutation(mode + "-" + label, base, mode, offset, fmt, value)
        header, strings, size = section(base.read_bytes(), ".shstrtab")
        mutation(mode + "-section-link", base, mode, header + 40, "<I", 2**32 - 1)
        mutation(mode + "-section-size", base, mode, header + 32, "<Q", 2**64 - 1)
        mutation(mode + "-section-name", base, mode, header, "<I", size)
        mutation(mode + "-unterminated-names", base, mode, strings + size - 1, "B", 65)

    for mode, base in [("host", host), ("target", target)]:
        header, offset, size = section(base.read_bytes(), ".dynsym")
        mutation(mode + "-symbol-name", base, mode, offset + 24, "<I", 2**32 - 1)
        mutation(mode + "-symbol-entry-size", base, mode, header + 56, "<Q", 1)
        header, offset, size = section(base.read_bytes(), ".gnu.version")
        mutation(mode + "-version-count", base, mode, header + 32, "<Q", size - 2)
        mutation(mode + "-version-type", base, mode, header + 4, "<I", 8)
        mutation(mode + "-unknown-version-index", base, mode, offset + 2, "<H", 0x7ffe)
        header, offset, size = section(base.read_bytes(), ".dynstr")
        mutation(mode + "-unterminated-dynstr", base, mode, offset + size - 1, "B", 65)

    for mode, base, name, fields in [
        ("host", host, ".gnu.version_d", [("count", 6, "<H", 65535), ("aux", 12, "<I", 2**32 - 1), ("next", 16, "<I", 2**32 - 1)]),
        ("target", target, ".gnu.version_r", [("count", 2, "<H", 65535), ("file", 4, "<I", 2**32 - 1), ("aux", 8, "<I", 2**32 - 1), ("next", 12, "<I", 16)]),
    ]:
        header, offset, size = section(base.read_bytes(), name)
        mutation(mode + "-empty-version-chain", base, mode, header + 32, "<Q", 0)
        mutation(mode + "-version-record-count", base, mode, header + 44, "<I", 65535)
        for label, field, fmt, value in fields:
            mutation(mode + "-version-" + label, base, mode, offset + field, fmt, value)
        aux_field = 12 if mode == "host" else 8
        aux = struct.unpack_from("<I", base.read_bytes(), offset + aux_field)[0]
        node = offset + aux
        mutation(mode + "-aux-name", base, mode, node + (0 if mode == "host" else 8), "<I", 2**32 - 1)
        mutation(mode + "-aux-next", base, mode, node + (4 if mode == "host" else 12), "<I", 2**32 - 1)

    for name in [".modinfo", "__versions"]:
        header, offset, size = section(module.read_bytes(), name)
        mutation("kmod-" + name + "-offset", module, "kmod", header + 24, "<Q", 2**64 - 1)
        mutation("kmod-" + name + "-type", module, "kmod", header + 4, "<I", 8)
    header, offset, size = section(module.read_bytes(), ".modinfo")
    data = bytearray(module.read_bytes())
    data[offset:offset + size] = b"x" * size
    cases.append(("kmod-unterminated-modinfo", "kmod", data))

    def invoke(mode, file):
        if mode == "kmod":
            args = ["-m", str(file), "-s", str(symvers), "-v", "new"]
        else:
            args = ["-c", str(file if mode == "host" else host), "-t", str(target if mode == "host" else file)]
        return subprocess.run([str(binary), *args], capture_output=True, text=True, timeout=20)

    for label, mode, data in cases:
        file = root / "malformed-libc.so.6"
        file.write_bytes(data)
        before = target.read_bytes()
        result = invoke(mode, file)
        assert result.returncode == 1, (label, result.returncode, result.stdout, result.stderr)
        assert file.read_bytes() == data and target.read_bytes() == before, label
        assert "适配成功" not in result.stdout and "适配完成" not in result.stdout, label
        assert "runtime error:" not in result.stderr and "AddressSanitizer" not in result.stderr, label
    # 有效的父版本链及不占文件空间的大 .bss 仍可解析。
    for mode, file in [("host", host), ("kmod", module)]:
        result = invoke(mode, file)
        assert result.returncode == 0, result.stdout + result.stderr
        assert "runtime error:" not in result.stderr and "AddressSanitizer" not in result.stderr
    print("PASS:", len(cases), "malformed ELF cases and 2 valid controls")
