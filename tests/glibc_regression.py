"""Run with: python3 tests/glibc_regression.py /path/to/cadapt"""
import pathlib
import shutil
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
