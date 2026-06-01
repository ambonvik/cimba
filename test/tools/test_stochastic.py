#!/usr/bin/env python3
"""
Stochastic test runner for Cimba.

For each registered test, runs the binary with a fixed seed and compares
stdout against a stored reference file.  Line endings are normalized before
comparison so the same reference file works on both Linux and Windows.

Generating / updating reference output:
    python test_stochastic.py --update

Running tests (called by meson test / CI):
    python test_stochastic.py

Exit code is 0 only if every test passes.
"""

import argparse
import os
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path


# ── Configuration ────────────────────────────────────────────────────────────

REPO_ROOT = Path(__file__).parent.parent.parent
BUILD_DIR = REPO_ROOT / "build" / "test"
REF_DIR   = Path(__file__).parent.parent / "reference"

# Executable extension — ".exe" on Windows, "" elsewhere.
EXE = ".exe" if sys.platform == "win32" else ""


@dataclass
class StochasticTest:
    """Describes one stochastic test binary."""
    name: str                   # used as the reference filename stem
    binary: str                 # executable name (without extension)
    seed: int                   # fixed seed for reproducibility
    extra_args: list = field(default_factory=list)  # any extra CLI arguments
    timeout: int = 90          # default timeout

    @property
    def ref_path(self) -> Path:
        return REF_DIR / f"{self.name}.txt"

    @property
    def binary_path(self) -> Path:
        return BUILD_DIR / f"{self.binary}{EXE}"

    def argv(self) -> list:
        return [str(self.binary_path), "-s", str(self.seed)] + self.extra_args


# Register stochastic tests here.
TESTS: list[StochasticTest] = [
    StochasticTest(name="buffer", binary="test_buffer", seed=0x34f05c64d7ad598f),
    StochasticTest(name="condition", binary="test_condition", seed=0x34f05c64d7ad598f),
    StochasticTest(name="data", binary="test_data", seed=0x34f05c64d7ad598f, timeout=300),
    StochasticTest(name="event", binary="test_event", seed=0x34f05c64d7ad598f),
    StochasticTest(name="hashheap", binary="test_hashheap", seed=0x34f05c64d7ad598f),
    StochasticTest(name="objectqueue", binary="test_objectqueue", seed=0x34f05c64d7ad598f),
    StochasticTest(name="priorityqueue", binary="test_priorityqueue", seed=0x34f05c64d7ad598f),
    StochasticTest(name="random",  binary="test_random",  seed=0x34f05c64d7ad598f, timeout=300),
    StochasticTest(name="resource", binary="test_resource", seed=0x34f05c64d7ad598f),
    StochasticTest(name="resourcepool", binary="test_resourcepool", seed=0x34f05c64d7ad598f),
    StochasticTest(name="cimba", binary="test_cimba", seed=0x34f05c64d7ad598f, extra_args=["-r", "1"], timeout=300),
]


# ── Helpers ───────────────────────────────────────────────────────────────────

RESET  = "\033[0m"
GREEN  = "\033[32m"
RED    = "\033[31m"
YELLOW = "\033[33m"
BOLD   = "\033[1m"

def _colour(text: str, code: str) -> str:
    """Wrap text in an ANSI colour code, but only when stdout is a terminal."""
    return f"{code}{text}{RESET}" if sys.stdout.isatty() else text

ok   = lambda s: _colour(s, GREEN)
fail = lambda s: _colour(s, RED)
warn = lambda s: _colour(s, YELLOW)
bold = lambda s: _colour(s, BOLD)


def normalise(raw: bytes) -> list[str]:
    """
    Decode and normalise output for comparison.

    - Decodes as UTF-8, replacing undecodable bytes so a partial output
      does not cause a confusing UnicodeDecodeError.
    - Splits on universal newlines, stripping all CR/LF variation.
    - Drops trailing blank lines so a missing final newline is not a
      spurious diff.
    """
    text = raw.decode("utf-8", errors="replace")
    lines = text.splitlines()           # handles \n, \r\n, \r uniformly
    while lines and not lines[-1].strip():
        lines.pop()
    return lines


def run_binary(test: StochasticTest) -> tuple[int, bytes, bytes]:
    env = os.environ.copy()

    # Check for .dll rather than sys.platform, since MSYS2 Python
    # reports sys.platform as 'msys' not 'win32'
    dll_path = BUILD_DIR.parent / "src" / "libcimba-1.dll"
    if dll_path.exists():
        env["PATH"] = str(dll_path.parent) + os.pathsep + env.get("PATH", "")

    try:
        result = subprocess.run(
            test.argv(),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=test.timeout,
            env=env,
        )
        return result.returncode, result.stdout, result.stderr
    except FileNotFoundError:
        print(fail(f"  [ERROR] Binary not found: {test.binary_path}"))
        return -1, b"", b""
    except subprocess.TimeoutExpired:
        print(fail(f"  [ERROR] Timed out after {test.timeout}s"))
        return -1, b"", b""

def diff_lines(expected: list[str], actual: list[str]) -> list[str]:
    """
    Return a compact unified-style diff.  We roll our own rather than
    importing difflib to keep the output concise for CI logs.
    """
    import difflib
    return list(difflib.unified_diff(
        expected, actual,
        fromfile="reference",
        tofile="actual",
        lineterm="",
    ))


# ── Core logic ────────────────────────────────────────────────────────────────

def update_reference(test: StochasticTest) -> bool:
    """Run the binary and write its output as the new reference."""
    print(f"  Updating reference for {bold(test.name)} (seed={test.seed}) ...")
    returncode, stdout, stderr = run_binary(test)
    if returncode != 0:
        print(fail(f"  [FAIL] Binary exited with code {returncode}; reference not updated."))
        if stderr:
            print("  stderr:", stderr.decode("utf-8", errors="replace").strip())
        return False

    REF_DIR.mkdir(parents=True, exist_ok=True)

    # Always write reference files with Unix line endings (\n) so the same
    # file is byte-identical on Linux and Windows and diffs cleanly in git.
    ref_lines = normalise(stdout)
    test.ref_path.write_text("\n".join(ref_lines) + "\n", encoding="utf-8")
    print(ok(f"  [OK]   Written {test.ref_path} ({len(ref_lines)} lines)"))
    return True


def verify_test(test: StochasticTest) -> bool:
    """Run the binary and compare output against the stored reference."""
    print(f"  Running {bold(test.name)} (seed={test.seed}) ...")

    if not test.binary_path.exists():
        print(fail(f"  [FAIL] Binary not found: {test.binary_path}"))
        return False

    if not test.ref_path.exists():
        print(fail(f"  [FAIL] No reference file: {test.ref_path}"))
        print(warn(f"         Run with --update to generate it."))
        return False

    returncode, stdout, stderr = run_binary(test)

    if returncode != 0:
        print(fail(f"  [FAIL] Binary exited with code {returncode}"))
        if stderr:
            print("  stderr:", stderr.decode("utf-8", errors="replace").strip())
        return False

    actual   = normalise(stdout)
    expected = normalise(test.ref_path.read_bytes())

    if actual == expected:
        print(ok(f"  [PASS] Output matches reference ({len(actual)} lines)"))
        return True

    print(fail(f"  [FAIL] Output differs from reference"))
    diff = diff_lines(expected, actual)
    # Print at most 40 diff lines to keep CI logs readable
    for line in diff[:40]:
        print(f"    {line}")
    if len(diff) > 40:
        print(warn(f"    ... ({len(diff) - 40} more lines omitted)"))
    return False


# ── Entry point ───────────────────────────────────────────────────────────────

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "--update", action="store_true",
        help="Regenerate reference files from current binary output",
    )
    parser.add_argument(
        "--build-dir", type=Path, default=None,
        help="Override the path to the Meson build directory",
    )
    parser.add_argument(
        "--test", metavar="NAME", action="append", default=None,
        help="Run only the named test(s); may be repeated",
    )
    args = parser.parse_args()

    if args.build_dir:
        global BUILD_DIR
        BUILD_DIR = args.build_dir

    tests = TESTS
    if args.test:
        tests = [t for t in TESTS if t.name in args.test]
        unknown = set(args.test) - {t.name for t in tests}
        for u in unknown:
            print(warn(f"Warning: no test named '{u}'"))

    if not tests:
        print(fail("No tests to run."))
        return 1

    action = "Updating" if args.update else "Verifying"
    print(bold(f"\n{action} {len(tests)} stochastic test(s)\n"))

    results = []
    for test in tests:
        if args.update:
            results.append(update_reference(test))
        else:
            results.append(verify_test(test))
        print()

    passed = sum(results)
    total  = len(results)
    failed = total - passed

    if args.update:
        print(bold(f"Updated {passed}/{total} reference file(s)."))
        return 0 if failed == 0 else 1

    # Summary line for CI logs
    if failed == 0:
        print(ok(bold(f"All {total} stochastic test(s) passed.")))
    else:
        print(fail(bold(f"{failed}/{total} stochastic test(s) FAILED.")))

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
