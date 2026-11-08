#!/usr/bin/env python3
"""Adversarial, fail-closed tests for the run-contained and run-exec CLI.

Black-box cases drive the built ios27 CLI with SDK-free inputs and check
the exit status and diagnostic of every case:

* guest startup vectors: 128 entries per vector, 4095-byte strings counted
  in bytes, and the 64 KiB payload shared by argv, envp and apple;
* option grammar: exact names, no inline values, option-looking values
  consumed as data, dangling options;
* --timeout-ms overflow, 32/64-bit wraparound aliases and non-decimal
  spellings (the CLI has no CPU-limit option; --timeout-ms is its only
  numeric process argument);
* --unsafe placement, and hostile image and loader paths: empty, missing,
  dangling or looping symlinks, FIFO, directory, character device,
  ENOTDIR, ENAMETOOLONG, unreadable, empty, truncated, oversized and
  wrong executables.

Any sanitizer report in the output fails the case. On native ARM64 the
largest accepted vectors also run through both runners, followed by
recovery runs. Embedded NUL bytes cannot cross execve, so unterminated
startup strings are unreachable here and belong to API-level tests.
"""

import os
import pathlib
import platform
import shutil
import subprocess
import sys
import tempfile

MAX_VECTOR = 128                     # IOS27_STARTUP_MAX_VECTOR
MAX_STRING = 4096                    # IOS27_STARTUP_MAX_STRING, with NUL
MAX_STRINGS = 64 * 1024              # IOS27_STARTUP_MAX_STRINGS, shared
MAX_INPUT_BYTES = 256 * 1024 * 1024  # MAX_INPUT_BYTES in src/main.c
VECTORS = ("--arg", "--env", "--apple")
SANITIZER_MARKERS = ("AddressSanitizer", "LeakSanitizer",
                     "UndefinedBehaviorSanitizer", "runtime error:")

REFUSED = "explicit --unsafe flag"
NEEDS_VALUE = "requires a value"
UNKNOWN = "unknown process option"
LIMITS = "exceed vector/string/payload limits"
TIMEOUT = "one decimal value from 1 through 60000"
UNREADABLE = "could not read file"
REJECTED = "mach-o rejected"
EXEC_FAILED = "cannot execute exec fixture"
ENTERED = "entry returned"

failures = []
executed = 0


def short(value):
    """ASCII-only label for a possibly long or non-UTF-8 argument."""
    if isinstance(value, bytes):
        text = value.decode("utf-8", "backslashreplace")
    else:
        text = str(value)
    if len(text) <= 40:
        return ascii(text)
    return f"{ascii(text[:12])}...<{len(text)} chars>"


def describe(arguments):
    shown = [short(argument) for argument in arguments[:10]]
    if len(arguments) > 10:
        shown.append(f"...<{len(arguments) - 10} more>")
    return " ".join(shown)


def check(name, arguments, codes, text=None, forbid=(), cwd=None,
          quiet=None):
    """Run one CLI case; record every mismatch instead of stopping.

    In process modes src/main.c writes only its success line to stdout,
    so a case that cannot exit 0 must leave stdout empty: any byte there
    is partial output. quiet=None derives this from codes, quiet=False
    keeps a case lenient and quiet=True forces the check.
    """
    global executed
    executed += 1
    codes = (codes,) if isinstance(codes, int) else tuple(codes)
    if quiet is None:
        quiet = 0 not in codes
    command = [argument if isinstance(argument, bytes)
               else os.fsencode(argument) for argument in arguments]
    try:
        result = subprocess.run(command, capture_output=True, timeout=20,
                                stdin=subprocess.DEVNULL, cwd=cwd,
                                check=False)
        code = result.returncode
        stdout = result.stdout
        output = (result.stdout + result.stderr).decode("utf-8", "replace")
    except subprocess.TimeoutExpired as error:
        code, output = None, "<no exit within 20 s>"
        stdout = error.stdout or b""
    problems = []
    if code not in codes:
        expected = " or ".join(str(value) for value in codes)
        problems.append(f"exit {code}, expected {expected}")
    if quiet and stdout:
        problems.append(f"{len(stdout)} unexpected stdout bytes")
    if text is not None and text not in output:
        problems.append(f"missing {text!r}")
    for marker in (*forbid, *SANITIZER_MARKERS):
        if marker in output:
            problems.append(f"unexpected {marker!r}")
    if problems:
        summary = "; ".join(problems)
        failures.append(f"{name}: {summary}\n"
                        f"    argv: {describe(arguments)}\n"
                        f"    output: {output.strip()[:600]}")


def full_vectors():
    """128 empty entries in each vector: 384 entries, 384 payload bytes."""
    return [item for option in VECTORS for item in [option, ""] * MAX_VECTOR]


def exact_payload():
    """Sixteen 4095-byte strings spread over all vectors: exactly 64 KiB."""
    longest = MAX_STRING - 1
    spread = []
    for option in VECTORS:
        spread += [option, "s" * longest] * 5
    spread += ["--apple", "t" * longest]
    assert (len(spread) // 2) * MAX_STRING == MAX_STRINGS
    return spread


def unsafe_placement(base, fixture, directory):
    """--unsafe is positional and exact, and is checked before anything."""
    missing = directory / "missing image"
    for flag in ("--UNSAFE", "--unsafe ", " --unsafe", "--unsafe=1",
                 "-unsafe", "unsafe", "", "--unsafe\n"):
        check(f"refuse flag {short(flag)}", [*base, fixture, flag], 2,
              REFUSED)
    check("flag before image", [*base, "--unsafe", fixture], 2, REFUSED)
    check("refusal precedes option parsing",
          [*base, missing, "--wrong", "--timeout-ms", "0"], 2, REFUSED)
    # A path literally named --unsafe is still only an image path.
    check("image named --unsafe", [*base, "--unsafe", "--unsafe"], 1,
          REJECTED, cwd=directory)


def option_grammar(base, missing):
    """Names are exact and case-sensitive; values never come inline."""
    accepted = [*base, missing, "--unsafe"]
    for name in ("--ARG", "--Env", "--APPLE", "--Timeout-ms", "--arg=x",
                 "--env=A=B", "--timeout-ms=5", "-arg", "--args",
                 "--timeout", "--", "-", "", "--unsafe"):
        check(f"unknown option {short(name)}", [*accepted, name, "value"],
              2, UNKNOWN)
    # Option-looking values are guest data, consumed with their option.
    check("option-looking values",
          [*accepted, "--arg", "--unsafe", "--env", "--timeout-ms",
           "--apple", "--arg", "--arg", "--apple"], 1, UNREADABLE)
    check("consumed value shifts pairing",
          [*accepted, "--arg", "--timeout-ms", "0"], 2,
          "requires a value: 0")
    check("dangling vector option", [*accepted, "--arg", "a", "--env"], 2,
          "requires a value: --env")
    check("dangling timeout", [*accepted, "--timeout-ms"], 2, NEEDS_VALUE)


def timeout_values(base, missing):
    """Deadlines are one in-range decimal; nothing may wrap into range."""
    accepted = [*base, missing, "--unsafe", "--timeout-ms"]
    rejected = (
        "00", "0" * 64, "60001", "65535", "65536", "99999", "600000",
        "4294967295", "4294967296", str(2 ** 32 + 1000),
        str(2 ** 32 + 60000), str(2 ** 64 - 1), str(2 ** 64),
        str(2 ** 64 + 1000), "9" * 4095,
        "0x10", "0X10", "1e3", "6e4", "1_000", "1,000", "1.0", "1.", ".5",
        "-0", "+0", "--1", "1-", " 60000", "60000 ", "\t1", "1\n",
        "１", "١", "1 ", b"\x80", b"\xff", b"1\xb0",
    )
    for value in rejected:
        check(f"timeout {short(value)}", [*accepted, value], 2, TIMEOUT)
    # Leading zeros leave the accumulator at zero, so long in-range
    # spellings pass: the overflow guard bounds magnitude, not length.
    for value in ("1", "01", "0" * 4000 + "1", "59999", "60000", "060000",
                  "0" * 4000 + "60000"):
        check(f"timeout {short(value)} accepted", [*accepted, value], 1,
              UNREADABLE)
    for repeat in (["5", "--timeout-ms", "5"], ["abc", "--timeout-ms", "5"],
                   ["5", "--arg", "x", "--timeout-ms", "6"]):
        label = " ".join(repeat)
        check(f"duplicate timeout {short(label)}", [*accepted, *repeat], 2,
              TIMEOUT)


def vector_bounds(base, missing):
    """Counts are per vector; strings and the payload count bytes."""
    accepted = [*base, missing, "--unsafe"]
    every = full_vectors()
    check("128 entries in every vector", [*accepted, *every], 1, UNREADABLE)
    for option in VECTORS:
        check(f"129th {option} with every vector full",
              [*accepted, *every, option, ""], 2, LIMITS)
    interleaved = ["--arg", "a", "--env", "e", "--apple", "p"] * MAX_VECTOR
    check("interleaved full vectors", [*accepted, *interleaved], 1,
          UNREADABLE)
    check("interleaving keeps counts",
          [*accepted, *interleaved, "--env", "e"], 2, LIMITS)
    spread = exact_payload()
    check("exact shared payload", [*accepted, *spread], 1, UNREADABLE)
    for option in VECTORS:
        # An empty string still costs its NUL byte.
        check(f"one byte over via {option}",
              [*accepted, *spread, option, ""], 2, LIMITS)
    accent = "é".encode("utf-8")
    check("4095 UTF-8 bytes", [*accepted, "--env", accent * 2047 + b"x"], 1,
          UNREADABLE)
    check("4096 UTF-8 bytes", [*accepted, "--env", accent * 2048], 2, LIMITS)
    check("oversized string", [*accepted, "--apple", "x" * 100000], 2,
          LIMITS)
    check("non-UTF-8 bytes are opaque data",
          [*accepted, "--arg", b"\xff\xfe\x80", "--apple", b"key=\xc3"], 1,
          UNREADABLE)


def hostile_paths(directory, fixture):
    """Create hostile paths; return (label, path, expected text) cases."""
    original = fixture.read_bytes()
    fifo = directory / "fifo"
    os.mkfifo(fifo)
    folder = directory / "folder"
    folder.mkdir()
    dangling = directory / "dangling link"
    dangling.symlink_to(directory / "nothing here")
    loop, loop_back = directory / "loop a", directory / "loop b"
    loop.symlink_to(loop_back)
    loop_back.symlink_to(loop)
    fifo_link = directory / "link to fifo"
    fifo_link.symlink_to(fifo)
    folder_link = directory / "link to folder"
    folder_link.symlink_to(folder)
    empty = directory / "empty image"
    empty.write_bytes(b"")
    oversized = directory / "oversized sparse image"
    with open(oversized, "wb") as handle:
        handle.truncate(MAX_INPUT_BYTES + 1)
    cases = [
        ("empty path", b"", UNREADABLE),
        ("missing", directory / "missing image", UNREADABLE),
        ("dangling symlink", dangling, UNREADABLE),
        ("symlink loop", loop, UNREADABLE),
        ("FIFO", fifo, UNREADABLE),
        ("symlink to FIFO", fifo_link, UNREADABLE),
        ("directory", folder, UNREADABLE),
        ("symlink to directory", folder_link, UNREADABLE),
        ("character device", pathlib.Path("/dev/null"), UNREADABLE),
        ("trailing slash", os.fsencode(fixture) + b"/", UNREADABLE),
        ("file as directory", fixture / "child", UNREADABLE),
        ("component over NAME_MAX", directory / ("n" * 300), UNREADABLE),
        ("path over PATH_MAX", directory.joinpath(*(["p"] * 2100)),
         UNREADABLE),
        ("oversized sparse file", oversized,
         f"exceeds {MAX_INPUT_BYTES} bytes"),
        ("empty file", empty, REJECTED),
    ]
    if os.geteuid() != 0:  # root bypasses file modes
        unreadable = directory / "unreadable image"
        unreadable.write_bytes(original)
        unreadable.chmod(0)
        cases.append(("unreadable file", unreadable, UNREADABLE))
    for size in (1, 4, 16, 31):
        prefix = directory / f"{size}-byte prefix"
        prefix.write_bytes(original[:size])
        cases.append((f"{size}-byte prefix", prefix, REJECTED))
    half = directory / "half image"
    half.write_bytes(original[:len(original) // 2])
    cases.append(("half image", half, None))
    return cases


def image_paths(base, cases):
    for label, path, text in cases:
        check(f"image {label}", [*base, path, "--unsafe"], 1, text,
              forbid=(ENTERED,))


def loader_paths(executable, loader, fixture, directory, cases, native):
    """Hostile loaders fail before or instead of guest entry; none hangs."""
    base = [executable, "run-exec"]
    check("exec without loader", [*base, fixture, "--unsafe"], 2, REFUSED)
    check("exec loader only", [*base, loader], 2, REFUSED)
    check("loader named --unsafe", [*base, "--unsafe", fixture, "--unsafe"],
          1, EXEC_FAILED, forbid=(ENTERED,), cwd=directory)
    for label, path, _ in cases:
        check(f"loader {label}", [*base, path, fixture, "--unsafe"], 1,
              EXEC_FAILED, forbid=(ENTERED,))
    script = directory / "script loader"
    script.write_text("#!/bin/sh\nexit 0\n")
    script.chmod(0o755)
    wrong = [("ios27 CLI", executable), ("shell script", script)]
    true = shutil.which("true")
    if true:
        wrong.append(("true(1)", pathlib.Path(true)))
    # Executable but wrong loaders never report readiness: a setup failure
    # or the deadline is acceptable, a guest result never is. A foreign
    # loader may write its own stdout, so those bytes stay unchecked.
    for label, path in wrong:
        check(f"wrong loader {label}",
              [*base, path, fixture, "--unsafe", "--timeout-ms", "500"],
              (1, 124), forbid=(ENTERED,), quiet=False)
    if native:
        check("exec recovery after hostile loaders",
              [*base, loader, fixture, "--unsafe"], 0,
              "exec ARM64 entry returned 101")


def native_boundaries(base, fixture, mode):
    """Largest accepted vectors reach the runner and stay contained."""
    accepted = [*base, fixture, "--unsafe"]
    returned = f"{mode} ARM64 entry returned"
    check(f"{mode}: 128 entries in every vector",
          [*accepted, *full_vectors()], 0, returned)
    # A small LC_MAIN stack may refuse the full 64 KiB payload, but only as
    # a clean setup failure: never a signal, deadline or sanitizer report.
    check(f"{mode}: exact shared payload", [*accepted, *exact_payload()],
          (0, 1), forbid=("terminated by signal", "exceeded"))
    check(f"{mode}: recovery", accepted, 0, f"{returned} 101")


def main():
    if not sys.platform.startswith("linux"):
        print("exec CLI adversarial: skipped, requires a Linux host")
        return 77
    if len(sys.argv) != 4:
        raise SystemExit("expected ios27, trusted ios27-loader and "
                         "phase19a fixture")
    sys.stdout.reconfigure(errors="backslashreplace")
    sys.stderr.reconfigure(errors="backslashreplace")
    executable, loader, fixture = (pathlib.Path(path).resolve()
                                   for path in sys.argv[1:4])
    native = platform.machine().lower() in {"aarch64", "arm64"}
    with tempfile.TemporaryDirectory(prefix="ios27 adversarial CLI ") as name:
        directory = pathlib.Path(name)
        missing = directory / "missing image"
        (directory / "--unsafe").write_bytes(b"not a Mach-O image")
        cases = hostile_paths(directory, fixture)
        for command, prefix, mode in (("run-contained", [], "contained"),
                                      ("run-exec", [loader], "exec")):
            base = [executable, command, *prefix]
            unsafe_placement(base, fixture, directory)
            option_grammar(base, missing)
            timeout_values(base, missing)
            vector_bounds(base, missing)
            image_paths(base, cases)
            if native:
                native_boundaries(base, fixture, mode)
        loader_paths(executable, loader, fixture, directory, cases, native)
    if failures:
        print(f"exec CLI adversarial: {len(failures)} of {executed} cases "
              "failed", file=sys.stderr)
        for failure in failures:
            print(failure, file=sys.stderr)
        return 1
    print(f"exec CLI adversarial: {executed} fail-closed cases passed; "
          + ("native runner boundaries and recovery passed" if native
             else "native runner boundaries require ARM64"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
