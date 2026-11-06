"""Run with: python3 tests/interp_regression.py /path/to/cadapt

First phase of interpreter support: only an equal or shorter new
interpreter path is accepted. A longer path, a nonexistent interpreter,
a non-ET_DYN interpreter, or a target without PT_INTERP must fail and
leave the file byte-for-byte unchanged. A patched executable must still
run through the new interpreter.
"""
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile


binary = pathlib.Path(sys.argv[1]).resolve()

# The replacement interpreter must live at a short absolute path, so the
# test uses fixed /tmp symlinks to the real loader instead of a path
# inside the temporary directory (which would be longer than the
# original interpreter string).
SHORT_LINK = pathlib.Path("/tmp/cadapt-ld.so")
EXE_AS_INTERP = pathlib.Path("/tmp/cadapt-exe")
NOT_ELF = pathlib.Path("/tmp/cadapt-not-elf")


def interp_of(data):
    phoff = struct.unpack_from("<Q", data, 32)[0]
    entsize, count = struct.unpack_from("<HH", data, 54)
    for i in range(count):
        p_type, _, p_offset, _, _, p_filesz, _, _ = struct.unpack_from(
            "<IIQQQQQQ", data, phoff + i * entsize)
        if p_type == 3:  # PT_INTERP
            end = data.index(0, p_offset, p_offset + p_filesz)
            return p_offset, p_filesz, data[p_offset:end].decode()
    return None


def interp_segment(data):
    result = interp_of(data)
    assert result is not None, "no PT_INTERP in test binary"
    return result


with tempfile.TemporaryDirectory(prefix="cadapt-interp-test-") as directory:
    root = pathlib.Path(directory)
    code = root / "hello.c"
    code.write_text('#include <stdio.h>\nint main(void) { puts("hello interp"); return 0; }\n')
    exe = root / "hello"
    subprocess.run(["cc", str(code), "-o", str(exe)], check=True)
    original = exe.read_bytes()
    _, _, old_interp = interp_segment(original)

    # Shorter replacement: a symlink in /tmp pointing at the real loader.
    for stale in (SHORT_LINK, EXE_AS_INTERP, NOT_ELF):
        if stale.is_symlink() or stale.exists():
            stale.unlink()
    SHORT_LINK.symlink_to(old_interp)
    shutil.copy2(exe, EXE_AS_INTERP)
    NOT_ELF.write_text("not an elf\n")
    # Equal-length replacement: same length as the original string.
    equal_path = "/tmp/" + "x" * (len(old_interp) - 8) + ".so"
    assert len(equal_path) == len(old_interp), equal_path
    equal_link = pathlib.Path(equal_path)
    if equal_link.is_symlink() or equal_link.exists():
        equal_link.unlink()
    equal_link.symlink_to(old_interp)

    def patched_copy(name):
        target = root / name
        shutil.copy2(exe, target)
        return target

    def check_patch(target, new_interp, succeeds=True):
        before = target.read_bytes()
        result = subprocess.run(
            [str(binary), "-i", new_interp, "-t", str(target)],
            capture_output=True, text=True, timeout=20)
        assert (result.returncode == 0) == succeeds, (
            new_interp, result.returncode, result.stdout, result.stderr)
        if not succeeds:
            assert target.read_bytes() == before, "failed run modified " + str(target)
            return before
        after = target.read_bytes()
        assert len(after) == len(before), "file size changed"
        offset, filesz, value = interp_segment(after)
        assert value == new_interp, (value, new_interp)
        # Bytes between the new NUL terminator and the end of the
        # original segment must be zero padding, not stale characters.
        assert after[offset + len(new_interp) + 1:offset + filesz] == b"\x00" * (
            filesz - len(new_interp) - 1)
        # The patched program must still execute through the new loader.
        run = subprocess.run([str(target)], capture_output=True, text=True, timeout=20)
        assert run.returncode == 0 and run.stdout == "hello interp\n", run
        return after

    try:
        # Print-only: reports the interpreter and changes nothing.
        target = patched_copy("print-target")
        before = target.read_bytes()
        result = subprocess.run(
            [str(binary), "-p", "-t", str(target)],
            capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stdout + result.stderr
        assert old_interp in result.stdout, result.stdout
        assert target.read_bytes() == before
        print("PASS: print-interp")

        # Dry run: reports the planned change and changes nothing.
        target = patched_copy("dry-target")
        before = target.read_bytes()
        result = subprocess.run(
            [str(binary), "--dry-run", "-i", str(SHORT_LINK), "-t", str(target)],
            capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stdout + result.stderr
        assert "试运行" in result.stdout, result.stdout
        assert target.read_bytes() == before
        print("PASS: dry-run")

        # Shorter path, then the already-set value (a no-op success).
        check_patch(patched_copy("short-target"), str(SHORT_LINK))
        print("PASS: shorter interpreter")
        target = patched_copy("noop-target")
        check_patch(target, old_interp)
        assert target.read_bytes() == original, "no-op run changed the file"
        print("PASS: same interpreter is a no-op")

        # Equal-length path.
        check_patch(patched_copy("equal-target"), equal_path)
        print("PASS: equal-length interpreter")

        # Longer path: rejected, file unchanged.
        check_patch(patched_copy("long-target"),
                    "/tmp/cadapt-ld-very-long-name-that-exceeds.so", succeeds=False)
        print("PASS: longer interpreter rejected")

        # Nonexistent interpreter (short enough to pass the length check).
        check_patch(patched_copy("missing-target"), "/tmp/no-such-ld.so", succeeds=False)
        print("PASS: nonexistent interpreter rejected")

        # Relative path: rejected by the CLI before any file access.
        check_patch(patched_copy("relative-target"), "ld.so", succeeds=False)
        print("PASS: relative interpreter rejected")

        # Not a dynamic linker: an ET_EXEC file and a non-ELF file.
        check_patch(patched_copy("exec-target"), str(EXE_AS_INTERP), succeeds=False)
        check_patch(patched_copy("text-target"), str(NOT_ELF), succeeds=False)
        print("PASS: non-loader interpreter rejected")

        # A shared library has no PT_INTERP.
        lib_code = root / "lib.c"
        lib_code.write_text("int entry(void) { return 1; }\n")
        lib = root / "lib.so"
        subprocess.run(
            ["cc", "-shared", "-fPIC", "-nostdlib", str(lib_code), "-o", str(lib)],
            check=True)
        lib_before = lib.read_bytes()
        result = subprocess.run(
            [str(binary), "-i", str(SHORT_LINK), "-t", str(lib)],
            capture_output=True, text=True, timeout=20)
        assert result.returncode != 0, result.stdout + result.stderr
        assert lib.read_bytes() == lib_before
        print("PASS: no-PT_INTERP target rejected")

        # Directory: the executable is patched, the library is skipped.
        batch = root / "batch"
        batch.mkdir()
        batch_exe = batch / "hello"
        shutil.copy2(exe, batch_exe)
        batch_lib = batch / "lib.so"
        shutil.copy2(lib, batch_lib)
        result = subprocess.run(
            [str(binary), "-i", str(SHORT_LINK), "-t", str(batch)],
            capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stdout + result.stderr
        assert interp_segment(batch_exe.read_bytes())[2] == str(SHORT_LINK)
        assert batch_lib.read_bytes() == lib_before
        run = subprocess.run([str(batch_exe)], capture_output=True, text=True, timeout=20)
        assert run.returncode == 0 and run.stdout == "hello interp\n", run
        print("PASS: directory scan")

        # Directory with no PT_INTERP at all: fails, changes nothing.
        only_lib = root / "only-lib"
        only_lib.mkdir()
        only = only_lib / "lib.so"
        shutil.copy2(lib, only)
        result = subprocess.run(
            [str(binary), "-i", str(SHORT_LINK), "-t", str(only_lib)],
            capture_output=True, text=True, timeout=20)
        assert result.returncode != 0, result.stdout + result.stderr
        assert only.read_bytes() == lib_before
        print("PASS: directory without interpreter rejected")
    finally:
        for stale in (SHORT_LINK, EXE_AS_INTERP, NOT_ELF, equal_link):
            try:
                if stale.is_symlink() or stale.exists():
                    stale.unlink()
            except OSError:
                pass

print("PASS: interp regression")
