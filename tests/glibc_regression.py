"""Run with: python3 tests/glibc_regression.py /path/to/cadapt"""
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile


binary = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="cadapt-glibc-test-") as directory:
    root = pathlib.Path(directory)

    def build_library(name, source, versions):
        folder = root / name
        folder.mkdir()
        code = folder / "library.c"
        script = folder / "versions.map"
        library = folder / "libc.so.6"
        code.write_text(source)
        script.write_text(versions)
        subprocess.run([
            "cc", "-shared", "-fPIC", "-nostdlib", str(code),
            "-Wl,-soname,libc.so.6", "-Wl,--version-script=" + str(script),
            "-o", str(library),
        ], check=True)
        return library

    host = build_library(
        "host", "int anchor(void) { return 1; } int foo(void) { return 2; }",
        "GLIBC_2.2.5 { global: anchor; foo; local: *; };",
    )
    newer = build_library(
        "newer", "int anchor(void) { return 1; } int foo(void) { return 2; } int absent(void) { return 3; }",
        "GLIBC_2.2.5 { global: anchor; local: *; }; GLIBC_9.0 { global: foo; absent; };",
    )

    def target(name, symbols):
        code = root / (name + ".c")
        output = root / (name + ".so")
        code.write_text(
            "\n".join("extern int " + symbol + "(void);" for symbol in symbols)
            + "\nint entry(void) { return " + "+".join(symbol + "()" for symbol in symbols) + "; }"
        )
        subprocess.run([
            "cc", "-shared", "-fPIC", "-nostdlib", str(code), str(newer), "-o", str(output),
        ], check=True)
        return output

    def run(path, succeeds, unchanged=False, libc=host):
        files = list(path.glob("*.so")) if path.is_dir() else [path]
        before = {file: file.read_bytes() for file in files}
        result = subprocess.run(
            [str(binary), "-c", str(libc), "-t", str(path)],
            capture_output=True, text=True, timeout=20,
        )
        assert (result.returncode == 0) == succeeds, result.stdout + result.stderr
        assert ("GLIBC 适配完成" in result.stdout) == succeeds, result.stdout
        if unchanged:
            for file, data in before.items():
                assert file.read_bytes() == data, "failed validation modified " + str(file)
            assert "修改 " not in result.stdout, result.stdout
        else:
            assert any(file.read_bytes() != data for file, data in before.items())
        print("PASS:", path.name)

    missing = target("missing-symbol", ["anchor", "foo", "absent"])
    run(missing, False, unchanged=True)
    unmapped = target("missing-version-id", ["foo"])
    run(unmapped, False, unchanged=True)
    valid = target("valid", ["anchor", "foo"])
    run(valid, True)

    # 同一目录中所有目标都检查失败时，不应修改任一文件或报告完成。
    batch = root / "failed-batch"
    batch.mkdir()
    shutil.copy2(missing, batch / missing.name)
    shutil.copy2(unmapped, batch / unmapped.name)
    run(batch, False, unchanged=True)
    run(missing, False, unchanged=True, libc=root / "missing-libc.so.6")

    # 从实际链接生成的 ELF 中读取版本依赖，再控制节点顺序以覆盖各删除位置。
    def sections(data):
        shoff = struct.unpack_from("<Q", data, 40)[0]
        entsize, count, names_index = struct.unpack_from("<HHH", data, 58)
        headers = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * entsize) for i in range(count)]
        names = headers[names_index]
        result = {}
        for header in headers:
            start = names[4] + header[0]
            name = data[start:data.index(0, start)].decode()
            result[name] = header
        return result

    def libc_requirements(data):
        tables = sections(data)
        strings = tables[".dynstr"][4]
        offset = tables[".gnu.version_r"][4]
        while True:
            _, count, file, aux, next_record = struct.unpack_from("<HHIII", data, offset)
            start = strings + file
            if data[start:data.index(0, start)] == b"libc.so.6":
                nodes = []
                node = offset + aux
                for i in range(count):
                    record = struct.unpack_from("<IHHII", data, node)
                    start = strings + record[3]
                    name = data[start:data.index(0, start)].decode()
                    nodes.append((node, name, record))
                    if i + 1 < count:
                        assert record[4] > 0, "version chain ended before vn_cnt"
                        node += record[4]
                    else:
                        assert record[4] == 0, "version chain extends past vn_cnt"
                return offset, nodes
            assert next_record, "missing libc dependency"
            offset += next_record

    chain_host = build_library(
        "chain-host",
        "int anchor(void) { return 1; } int foo(void) { return 2; } int anchor2(void) { return 3; } int bar(void) { return 4; }",
        "GLIBC_2.2.5 { global: anchor; foo; local: *; }; GLIBC_2.3 { global: anchor2; bar; };",
    )
    newer = build_library(
        "chain-newer",
        "int anchor(void) { return 1; } int foo(void) { return 2; } int anchor2(void) { return 3; } int bar(void) { return 4; }",
        "GLIBC_2.2.5 { global: anchor; local: *; }; GLIBC_2.3 { global: anchor2; }; GLIBC_9.0 { global: foo; }; GLIBC_10.0 { global: bar; };",
    )
    kept = ["GLIBC_2.2.5", "GLIBC_2.3"]
    removed = ["GLIBC_9.0", "GLIBC_10.0"]
    orders = {
        "middle": [kept[0], removed[0], kept[1], removed[1]],
        "consecutive-middle": [kept[0], *removed, kept[1]],
        "head": [*removed, *kept],
        "tail": [*kept, *removed],
    }
    for label, order in orders.items():
        output = target("chain-" + label, ["anchor", "anchor2", "foo", "bar"])
        data = bytearray(output.read_bytes())
        need, nodes = libc_requirements(data)
        records = {name: record for _, name, record in nodes}
        slots = sorted(node for node, _, _ in nodes)
        assert len(slots) == len(order)
        struct.pack_into("<I", data, need + 8, slots[0] - need)
        for i, name in enumerate(order):
            next_offset = slots[i + 1] - slots[i] if i + 1 < len(slots) else 0
            struct.pack_into("<IHHII", data, slots[i], *records[name][:4], next_offset)
        output.write_bytes(data)
        run(output, True, libc=chain_host)
        patched = output.read_bytes()
        _, nodes = libc_requirements(patched)
        assert [name for _, name, _ in nodes] == kept, nodes
        ids = {record[2]: name for _, name, record in nodes}
        tables = sections(patched)
        strings = tables[".dynstr"][4]
        dynsym = tables[".dynsym"]
        expected = {"anchor": kept[0], "foo": kept[0], "anchor2": kept[1], "bar": kept[1]}
        seen = set()
        for i in range(dynsym[5] // dynsym[9]):
            name_offset = struct.unpack_from("<I", patched, dynsym[4] + i * dynsym[9])[0]
            start = strings + name_offset
            name = patched[start:patched.index(0, start)].decode()
            if name in expected:
                version = struct.unpack_from("<H", patched, tables[".gnu.version"][4] + i * 2)[0]
                assert ids[version] == expected[name], (name, version, ids)
                seen.add(name)
        assert seen == set(expected)
        assert len(patched) == len(data)
