"""Run with: python3 tests/rpath_regression.py /path/to/cadapt

RPATH support: an equal or shorter new path is patched in place inside
.dynstr (the remaining bytes are zeroed). A longer path rewrites the
file patchelf-style: the new .dynstr is appended in a new PT_LOAD
segment at the end of the file, DT_STRTAB/DT_STRSZ and the RPATH entry
are re-pointed, and the program must still run using the new RPATH. A
target without DT_RPATH/DT_RUNPATH gets a new DT_RUNPATH entry (growing
.dynamic by one slot) the same way. A relative path component must fail
and leave the file byte-for-byte unchanged.
"""
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile


binary = pathlib.Path(sys.argv[1]).resolve()

DT_RPATH = 15
DT_RUNPATH = 29


def dynamic_entries(data):
    phoff = struct.unpack_from("<Q", data, 32)[0]
    entsize, count = struct.unpack_from("<HH", data, 54)
    for i in range(count):
        p_type, _, p_offset, _, _, p_filesz, _, _ = struct.unpack_from(
            "<IIQQQQQQ", data, phoff + i * entsize)
        if p_type == 2:  # PT_DYNAMIC
            entries = []
            for j in range(p_filesz // 16):
                tag, val = struct.unpack_from("<qQ", data, p_offset + j * 16)
                entries.append((tag, val))
                if tag == 0:
                    break
            return entries
    return None


def dynstr(data):
    shoff = struct.unpack_from("<Q", data, 0x28)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3a)
    headers = []
    for i in range(shnum):
        sh_name, _, _, _, sh_offset, sh_size = struct.unpack_from(
            "<IIQQQQ", data, shoff + i * shentsize)
        headers.append((sh_name, sh_offset, sh_size))
    str_off = headers[shstrndx][1]
    for sh_name, sh_offset, sh_size in headers:
        end = data.index(0, str_off + sh_name)
        if data[str_off + sh_name:end] == b".dynstr":
            return sh_offset, sh_size
    return None


def dynstr_addr(data):
    """Return the .dynstr section's sh_addr."""
    shoff = struct.unpack_from("<Q", data, 0x28)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3a)
    headers = []
    for i in range(shnum):
        sh_name, _, _, sh_addr, sh_offset, sh_size = struct.unpack_from(
            "<IIQQQQ", data, shoff + i * shentsize)
        headers.append((sh_name, sh_addr, sh_offset, sh_size))
    str_off = headers[shstrndx][2]
    for sh_name, sh_addr, sh_offset, sh_size in headers:
        end = data.index(0, str_off + sh_name)
        if data[str_off + sh_name:end] == b".dynstr":
            return sh_addr
    return None


def load_segments(data):
    """Return a list of (p_type, p_offset, p_vaddr, p_filesz) for PT_LOAD."""
    phoff = struct.unpack_from("<Q", data, 32)[0]
    entsize, count = struct.unpack_from("<HH", data, 54)
    loads = []
    for i in range(count):
        p_type, _, p_offset, p_vaddr, _, p_filesz, _, _ = struct.unpack_from(
            "<IIQQQQQQ", data, phoff + i * entsize)
        if p_type == 1:  # PT_LOAD
            loads.append((p_type, p_offset, p_vaddr, p_filesz))
    return loads


def rpath_of(data):
    """Return (tag, value, strtab_offset) or None."""
    entries = dynamic_entries(data)
    assert entries is not None, "no PT_DYNAMIC in test binary"
    chosen = None
    for tag, val in entries:
        if tag == DT_RUNPATH:
            chosen = (tag, val)
            break
        if tag == DT_RPATH and chosen is None:
            chosen = (tag, val)
    if chosen is None:
        return None
    tag, val = chosen
    strtab = dynstr(data)
    assert strtab is not None, "no .dynstr section"
    sh_offset, sh_size = strtab
    assert val < sh_size, "RPATH offset outside .dynstr"
    start = sh_offset + val
    end = data.index(0, start, sh_offset + sh_size)
    return tag, data[start:end].decode(), val


def rpath_value(data):
    result = rpath_of(data)
    assert result is not None, "no RPATH in test binary"
    return result


with tempfile.TemporaryDirectory(prefix="cadapt-rpath-test-") as directory:
    root = pathlib.Path(directory)
    old_dir = root / "aaaaaaaa-old-libdir"
    new_dir = root / "new"
    empty_dir = root / "empty"
    origin_lib = root / "lib"
    for d in (old_dir, new_dir, empty_dir, origin_lib):
        d.mkdir()
    foo_c = root / "foo.c"
    foo_c.write_text("int foo(void) { return 42; }\n")
    lib = old_dir / "libfoo.so"
    subprocess.run(
        ["cc", "-shared", "-fPIC", str(foo_c), "-o", str(lib)], check=True)
    lib_before = lib.read_bytes()
    shutil.copy2(lib, new_dir / "libfoo.so")
    shutil.copy2(lib, origin_lib / "libfoo.so")

    main_c = root / "main.c"
    main_c.write_text(
        '#include <stdio.h>\n'
        'extern int foo(void);\n'
        'int main(void) { printf("foo=%d\\n", foo()); return 0; }\n')

    def build_exe(name, extra_flags):
        exe = root / name
        subprocess.run(
            ["cc", str(main_c), "-L" + str(old_dir), "-lfoo",
             *extra_flags, "-o", str(exe)],
            check=True)
        return exe

    exe_runpath = build_exe("exe-runpath", ["-Wl,-rpath," + str(old_dir)])
    exe_rpath = build_exe(
        "exe-rpath", ["-Wl,--disable-new-dtags,-rpath," + str(old_dir)])
    exe_plain = build_exe("exe-plain", [])
    tag, old_rpath, _ = rpath_value(exe_runpath.read_bytes())
    assert tag == DT_RUNPATH, tag
    tag, old_rpath2, _ = rpath_value(exe_rpath.read_bytes())
    assert tag == DT_RPATH, tag
    assert old_rpath == old_rpath2 == str(old_dir)
    assert rpath_of(exe_plain.read_bytes()) is None
    run = subprocess.run([str(exe_runpath)], capture_output=True, text=True, timeout=20)
    assert run.returncode == 0 and run.stdout == "foo=42\n", run

    def patched_copy(name, source=exe_runpath):
        target = root / name
        shutil.copy2(source, target)
        return target

    def check_patch(target, new_rpath, succeeds=True, runs=True):
        before = target.read_bytes()
        _, old_value, _ = rpath_value(before)
        result = subprocess.run(
            [str(binary), "-r", new_rpath, "-t", str(target)],
            capture_output=True, text=True, timeout=20)
        assert (result.returncode == 0) == succeeds, (
            new_rpath, result.returncode, result.stdout, result.stderr)
        if not succeeds:
            assert target.read_bytes() == before, "failed run modified " + str(target)
            return before
        after = target.read_bytes()
        assert len(after) == len(before), "file size changed"
        tag, value, offset = rpath_value(after)
        assert value == new_rpath, (value, new_rpath)
        # Bytes between the new NUL terminator and the end of the old
        # string must be zero padding, not stale characters.
        strtab_off, _ = dynstr(after)
        assert after[strtab_off + offset + len(new_rpath) + 1:
                     strtab_off + offset + len(old_value) + 1] == b"\x00" * (
            len(old_value) - len(new_rpath))
        if runs:
            run = subprocess.run([str(target)], capture_output=True, text=True, timeout=20)
            assert run.returncode == 0 and run.stdout == "foo=42\n", run
        return after

    def check_rewrite(target, new_rpath, runs=True, had_rpath=True):
        """Longer RPATH (or no RPATH): file is rewritten, must still run."""
        before = target.read_bytes()
        old = rpath_of(before)
        old_value = old[1] if old else None
        old_loads = len(load_segments(before))
        result = subprocess.run(
            [str(binary), "-r", new_rpath, "-t", str(target)],
            capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, (
            new_rpath, result.returncode, result.stdout, result.stderr)
        assert "重写文件" in result.stdout, result.stdout
        after = target.read_bytes()
        assert len(after) > len(before), "rewrite did not grow the file"
        tag, value, offset = rpath_value(after)
        assert value == new_rpath, (value, new_rpath)
        # The new .dynstr is append-only: the old string is still there
        # as dead bytes, so every old string offset stays valid.
        strtab_off, strtab_size = dynstr(after)
        if old_value is not None:
            assert old_value.encode() in after[strtab_off:strtab_off + strtab_size]
        # DT_STRTAB must point at the moved .dynstr, and a new PT_LOAD
        # must cover it.
        strtab_addr = dynstr_addr(after)
        entries = dict(dynamic_entries(after))
        assert entries.get(5) == strtab_addr, (entries.get(5), strtab_addr)  # DT_STRTAB
        assert entries.get(10) == strtab_size, (entries.get(10), strtab_size)  # DT_STRSZ
        new_loads = load_segments(after)
        assert len(new_loads) == old_loads + 1, (len(new_loads), old_loads)
        last = new_loads[-1]
        assert last[1] + last[3] <= len(after), last
        assert strtab_off >= last[1] and strtab_off < last[1] + last[3], (
            strtab_off, last)
        if runs:
            run = subprocess.run([str(target)], capture_output=True, text=True, timeout=20)
            assert run.returncode == 0 and run.stdout == "foo=42\n", run
        return after

    try:
        # Print-only: reports the RPATH and changes nothing.
        target = patched_copy("print-target")
        before = target.read_bytes()
        result = subprocess.run(
            [str(binary), "--print-rpath", "-t", str(target)],
            capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stdout + result.stderr
        assert "RUNPATH=" + str(old_dir) in result.stdout, result.stdout
        assert target.read_bytes() == before
        print("PASS: print-rpath")

        # Dry run: reports the planned change and changes nothing.
        target = patched_copy("dry-target")
        before = target.read_bytes()
        result = subprocess.run(
            [str(binary), "--dry-run", "-r", str(new_dir), "-t", str(target)],
            capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stdout + result.stderr
        assert "试运行" in result.stdout, result.stdout
        assert target.read_bytes() == before
        print("PASS: dry-run")

        # Shorter path to a directory that also holds the library:
        # the program still runs, now via the new RPATH.
        check_patch(patched_copy("short-target"), str(new_dir))
        print("PASS: shorter RPATH, still runs")

        # Shorter path to a directory without the library: the string
        # changes, but the program can no longer find its library.
        target = patched_copy("nolib-target")
        check_patch(target, str(empty_dir), runs=False)
        run = subprocess.run([str(target)], capture_output=True, text=True, timeout=20)
        assert run.returncode != 0 and "libfoo.so" in run.stderr, run
        print("PASS: RPATH actually used at runtime")

        # $ORIGIN is accepted and resolves relative to the executable.
        check_patch(patched_copy("origin-target"), "$ORIGIN/lib")
        print("PASS: $ORIGIN RPATH")

        # Same value is a no-op success.
        target = patched_copy("noop-target")
        check_patch(target, str(old_dir))
        assert target.read_bytes() == exe_runpath.read_bytes(), "no-op run changed the file"
        print("PASS: same RPATH is a no-op")

        # Equal-length path.
        equal_path = "/" + "x" * (len(str(old_dir)) - 1)
        assert len(equal_path) == len(str(old_dir)), equal_path
        check_patch(patched_copy("equal-target"), equal_path, runs=False)
        print("PASS: equal-length RPATH")

        # Clearing with an empty string.
        target = patched_copy("clear-target")
        check_patch(target, "", runs=False)
        run = subprocess.run([str(target)], capture_output=True, text=True, timeout=20)
        assert run.returncode != 0 and "libfoo.so" in run.stderr, run
        print("PASS: clear RPATH")

        # Longer path: the file is rewritten (patchelf-style), the new
        # RPATH is actually used at runtime, and the old string survives
        # as dead bytes in the appended .dynstr.
        long_dir = root / "a-much-longer-libdir-name"
        long_dir.mkdir()
        shutil.copy2(lib, long_dir / "libfoo.so")
        check_rewrite(patched_copy("long-target"), str(long_dir))
        print("PASS: longer RPATH rewrites the file and still runs")

        # Dry run of a longer path: reports the rewrite plan, changes nothing.
        target = patched_copy("long-dry-target")
        before = target.read_bytes()
        result = subprocess.run(
            [str(binary), "--dry-run", "-r", str(long_dir), "-t", str(target)],
            capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stdout + result.stderr
        assert "试运行" in result.stdout and "重写文件" in result.stdout, result.stdout
        assert target.read_bytes() == before
        print("PASS: dry-run of a longer RPATH")

        # Relative path component: rejected, file unchanged.
        check_patch(patched_copy("relative-target"), "relative/dir", succeeds=False)
        check_patch(patched_copy("mixed-target"),
                    str(new_dir) + ":relative/dir", succeeds=False)
        print("PASS: relative RPATH rejected")

        # Old-style DT_RPATH target: shorter replacement keeps the tag.
        target = patched_copy("oldtag-target", exe_rpath)
        after = check_patch(target, str(new_dir))
        tag, _, _ = rpath_value(after)
        assert tag == DT_RPATH, tag
        print("PASS: DT_RPATH target")

        # Old-style DT_RPATH target with a longer path: rewritten, tag kept.
        target = patched_copy("oldtag-long-target", exe_rpath)
        after = check_rewrite(target, str(long_dir))
        tag, _, _ = rpath_value(after)
        assert tag == DT_RPATH, tag
        print("PASS: DT_RPATH target, longer path")

        # A binary without any RPATH: a new DT_RUNPATH is added (growing
        # .dynamic by one slot) and the program runs via the new RPATH.
        target = patched_copy("plain-target", exe_plain)
        check_rewrite(target, str(new_dir), had_rpath=False)
        print("PASS: RPATH added to a binary without one")

        # A shared library has no RPATH either: one is added.
        lib_target = root / "libfoo-patched.so"
        shutil.copy2(lib, lib_target)
        before = lib_target.read_bytes()
        check_rewrite(lib_target, str(new_dir), runs=False, had_rpath=False)
        print("PASS: RPATH added to a library")

        # Directory: the executable is patched in place (shorter RPATH)
        # and the library gains an RPATH (it had none).
        batch = root / "batch"
        batch.mkdir()
        batch_exe = batch / "hello"
        shutil.copy2(exe_runpath, batch_exe)
        batch_lib = batch / "libfoo.so"
        shutil.copy2(lib, batch_lib)
        result = subprocess.run(
            [str(binary), "-r", str(new_dir), "-t", str(batch)],
            capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stdout + result.stderr
        assert rpath_value(batch_exe.read_bytes())[1] == str(new_dir)
        assert rpath_value(batch_lib.read_bytes())[1] == str(new_dir)
        run = subprocess.run([str(batch_exe)], capture_output=True, text=True, timeout=20)
        assert run.returncode == 0 and run.stdout == "foo=42\n", run
        print("PASS: directory scan")

        # Directory with only a library without RPATH: the library gets
        # an RPATH added, so the run succeeds.
        only_lib = root / "only-lib"
        only_lib.mkdir()
        only = only_lib / "libfoo.so"
        shutil.copy2(lib, only)
        result = subprocess.run(
            [str(binary), "-r", str(new_dir), "-t", str(only_lib)],
            capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stdout + result.stderr
        assert rpath_value(only.read_bytes())[1] == str(new_dir)
        print("PASS: directory with only a library gains an RPATH")

        # Directory with a longer RPATH: both files are rewritten.
        batch2 = root / "batch2"
        batch2.mkdir()
        batch2_exe = batch2 / "hello"
        shutil.copy2(exe_runpath, batch2_exe)
        batch2_lib = batch2 / "libfoo.so"
        shutil.copy2(lib, batch2_lib)
        result = subprocess.run(
            [str(binary), "-r", str(long_dir), "-t", str(batch2)],
            capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stdout + result.stderr
        assert rpath_value(batch2_exe.read_bytes())[1] == str(long_dir)
        assert rpath_value(batch2_lib.read_bytes())[1] == str(long_dir)
        run = subprocess.run([str(batch2_exe)], capture_output=True, text=True, timeout=20)
        assert run.returncode == 0 and run.stdout == "foo=42\n", run
        print("PASS: directory with a longer RPATH")

        # Combined with --print-interp: both print, nothing changes.
        target = patched_copy("both-print-target")
        before = target.read_bytes()
        result = subprocess.run(
            [str(binary), "-p", "--print-rpath", "-t", str(target)],
            capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stdout + result.stderr
        assert "RUNPATH=" + str(old_dir) in result.stdout, result.stdout
        assert target.read_bytes() == before
        print("PASS: print interp and rpath together")
    finally:
        pass

print("PASS: rpath regression")
