"""Run with: python3 tests/interp_regression.py /path/to/cadapt

Interpreter support: an equal or shorter new path is patched in place
(the remaining segment bytes are zeroed). A longer path rewrites the
file: the new string is appended at the end and PT_INTERP is repointed
to it, while the rest of the file is untouched. A nonexistent
interpreter, a non-ET_DYN interpreter, or a target without PT_INTERP
must fail and leave the file byte-for-byte unchanged. A patched
executable must still run through the new interpreter.
"""
import pathlib
import shutil
import stat
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
LONG_LINK = pathlib.Path("/tmp/cadapt-longer-interp-ld.so")
VERY_LONG_LINK = pathlib.Path("/tmp/" + "y" * 80 + ".so")
LONG_EXE = pathlib.Path("/tmp/cadapt-much-longer-exe-path")
EXE_AS_INTERP = pathlib.Path("/tmp/cadapt-exe")
NOT_ELF = pathlib.Path("/tmp/cadapt-not-elf")
MISSING_LONG = "/tmp/no-such-much-longer-ld.so"


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


def sections(data):
    shoff = struct.unpack_from("<Q", data, 0x28)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3a)
    headers = []
    for i in range(shnum):
        sh_name, _, _, sh_addr, sh_offset, sh_size = struct.unpack_from(
            "<IIQQQQ", data, shoff + i * shentsize)
        headers.append((sh_name, sh_addr, sh_offset, sh_size))
    str_off = headers[shstrndx][2]
    result = {}
    for sh_name, sh_addr, sh_offset, sh_size in headers:
        end = data.index(0, str_off + sh_name)
        result[data[str_off + sh_name:end].decode()] = (sh_offset, sh_size, sh_addr)
    return result


def check_load_alignment(data):
    phoff = struct.unpack_from("<Q", data, 32)[0]
    entsize, count = struct.unpack_from("<HH", data, 54)
    for i in range(count):
        p_type, _, p_offset, p_vaddr, _, _, _, p_align = struct.unpack_from(
            "<IIQQQQQQ", data, phoff + i * entsize)
        if p_type == 1 and p_align > 1:  # PT_LOAD
            assert p_offset % p_align == p_vaddr % p_align, "LOAD misaligned"


with tempfile.TemporaryDirectory(prefix="cadapt-interp-test-") as directory:
    root = pathlib.Path(directory)
    code = root / "hello.c"
    code.write_text('#include <stdio.h>\nint main(void) { puts("hello interp"); return 0; }\n')
    exe = root / "hello"
    subprocess.run(["cc", str(code), "-o", str(exe)], check=True)
    original = exe.read_bytes()
    _, _, old_interp = interp_segment(original)

    # Shorter replacement: a symlink in /tmp pointing at the real loader.
    for stale in (SHORT_LINK, LONG_LINK, VERY_LONG_LINK, LONG_EXE,
                  EXE_AS_INTERP, NOT_ELF):
        if stale.is_symlink() or stale.exists():
            stale.unlink()
    SHORT_LINK.symlink_to(old_interp)
    LONG_LINK.symlink_to(old_interp)
    VERY_LONG_LINK.symlink_to(old_interp)
    assert len(str(LONG_LINK)) > len(old_interp), str(LONG_LINK)
    assert len(str(VERY_LONG_LINK)) > len(old_interp), str(VERY_LONG_LINK)
    assert len(MISSING_LONG) > len(old_interp), MISSING_LONG
    shutil.copy2(exe, EXE_AS_INTERP)
    shutil.copy2(exe, LONG_EXE)
    assert len(str(LONG_EXE)) > len(old_interp), str(LONG_EXE)
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

    def check_patch_long(target, new_interp):
        before = target.read_bytes()
        mode_before = stat.S_IMODE(target.stat().st_mode)
        result = subprocess.run(
            [str(binary), "-i", new_interp, "-t", str(target)],
            capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, (new_interp, result.stdout, result.stderr)
        after = target.read_bytes()
        # The new string is appended at the old end of file.
        assert len(after) == len(before) + len(new_interp) + 1, (
            len(before), len(after))
        offset, filesz, value = interp_segment(after)
        assert offset == len(before), (offset, len(before))
        assert filesz == len(new_interp) + 1, filesz
        assert value == new_interp, (value, new_interp)
        sec = sections(after).get(".interp")
        assert sec is not None, "no .interp section after rewrite"
        sec_offset, sec_size, _ = sec
        assert sec_offset == len(before) and sec_size == len(new_interp) + 1, sec
        assert after[sec_offset:sec_offset + sec_size - 1].decode() == new_interp
        check_load_alignment(after)
        assert stat.S_IMODE(target.stat().st_mode) == mode_before, "mode changed"
        if shutil.which("readelf"):
            for args in (["-l"], ["-S"]):
                parsed = subprocess.run(
                    ["readelf", *args, str(target)],
                    capture_output=True, text=True, timeout=20)
                assert parsed.returncode == 0, (args, parsed.stdout, parsed.stderr)
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

        # Longer path: rewrites the file (string appended at the end).
        check_patch_long(patched_copy("long-target"), str(LONG_LINK))
        print("PASS: longer interpreter rewrites file")

        # Much longer path (88 chars): same rewrite path.
        check_patch_long(patched_copy("very-long-target"), str(VERY_LONG_LINK))
        print("PASS: much longer interpreter rewrites file")

        # Longer path, dry run: reports the rewrite and changes nothing.
        target = patched_copy("long-dry-target")
        before = target.read_bytes()
        result = subprocess.run(
            [str(binary), "--dry-run", "-i", str(LONG_LINK), "-t", str(target)],
            capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stdout + result.stderr
        assert "需重写文件" in result.stdout, result.stdout
        assert target.read_bytes() == before
        print("PASS: longer interpreter dry-run")

        # Rewriting to the same longer value twice: the second run is a no-op.
        target = patched_copy("long-noop-target")
        check_patch_long(target, str(LONG_LINK))
        size_after_first = len(target.read_bytes())
        result = subprocess.run(
            [str(binary), "-i", str(LONG_LINK), "-t", str(target)],
            capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stdout + result.stderr
        assert len(target.read_bytes()) == size_after_first
        print("PASS: longer interpreter no-op on second run")

        # Rewrite, then a shorter path again: in-place patch on the
        # rewritten file, size unchanged, still runs.
        target = patched_copy("roundtrip-target")
        check_patch_long(target, str(LONG_LINK))
        size_after_rewrite = len(target.read_bytes())
        check_patch(target, str(SHORT_LINK))
        assert len(target.read_bytes()) == size_after_rewrite
        print("PASS: rewrite then shorter path round-trip")

        # Longer path to a nonexistent interpreter: rejected before any
        # rewrite, file unchanged.
        check_patch(patched_copy("long-missing-target"), MISSING_LONG, succeeds=False)
        print("PASS: longer nonexistent interpreter rejected")

        # Longer path to a non-ET_DYN file: rejected before any rewrite.
        check_patch(patched_copy("long-exec-target"), str(LONG_EXE), succeeds=False)
        print("PASS: longer non-loader interpreter rejected")

        # Symlink target with a longer path: the rewrite must refuse
        # (rename would replace the link), link and file unchanged.
        link_target = patched_copy("link-real")
        link = root / "link-target"
        link.symlink_to(link_target)
        link_before = link_target.read_bytes()
        result = subprocess.run(
            [str(binary), "-i", str(LONG_LINK), "-t", str(link)],
            capture_output=True, text=True, timeout=20)
        assert result.returncode != 0, result.stdout + result.stderr
        assert link.is_symlink() and link.readlink() == link_target
        assert link_target.read_bytes() == link_before
        print("PASS: longer interpreter rejects symlink target")

        # Directory scan with longer interpreter is covered after the
        # shared-library fixture is built below.

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

        # Longer path in a directory: the executable is rewritten, the
        # library is skipped.
        batch2 = root / "batch2"
        batch2.mkdir()
        batch2_exe = batch2 / "hello"
        shutil.copy2(exe, batch2_exe)
        batch2_lib = batch2 / "lib.so"
        shutil.copy2(lib, batch2_lib)
        result = subprocess.run(
            [str(binary), "-i", str(LONG_LINK), "-t", str(batch2)],
            capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stdout + result.stderr
        assert interp_segment(batch2_exe.read_bytes())[2] == str(LONG_LINK)
        assert batch2_lib.read_bytes() == lib_before
        run = subprocess.run([str(batch2_exe)], capture_output=True, text=True, timeout=20)
        assert run.returncode == 0 and run.stdout == "hello interp\n", run
        print("PASS: directory scan with longer interpreter")
    finally:
        for stale in (SHORT_LINK, LONG_LINK, VERY_LONG_LINK, LONG_EXE,
                      EXE_AS_INTERP, NOT_ELF, equal_link):
            try:
                if stale.is_symlink() or stale.exists():
                    stale.unlink()
            except OSError:
                pass

print("PASS: interp regression")
