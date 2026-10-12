"""Run with: python3 tests/cli_regression.py /path/to/cadapt"""
import pathlib
import subprocess
import sys


binary = pathlib.Path(sys.argv[1]).resolve()
cases = [
    ([], 0), (["--help"], 0), (["-h"], 0),
    (["--help", "-c", "/nonexistent/libc.so.6"], 0),
    (["--unknown"], 1), (["-c"], 1), (["-m"], 1),
    (["--help", "--unknown"], 1), (["--unknown", "--help"], 1),
    (["--help", "--libc"], 1), (["--help", "extra"], 1),
    (["-c", "/nonexistent/libc.so.6"], 1), (["-t", "target"], 1),
]
for args, expected in cases:
    result = subprocess.run([str(binary), *args], capture_output=True, text=True, timeout=10)
    assert result.returncode == expected, (args, result.returncode, result.stdout, result.stderr)
    assert "Usage:" in result.stdout, args
    assert "==============" not in result.stdout, "help invocation started a modification"
    if expected == 0:
        assert not result.stderr, (args, result.stderr)
print("PASS:", len(cases), "CLI cases")
