#!/usr/bin/env python3
"""Test process CLI parsing and native exit-status mapping with SDK-free fixtures.

Native fault cases explicitly inject ARM64 instructions into temporary copies
of the build fixture. x86_64 checks host refusal rather than native execution.
"""

import os
import pathlib
import platform
import signal
import struct
import subprocess
import sys
import tempfile


def check(arguments, expected, text):
    result = subprocess.run(
        [str(arg) for arg in arguments], capture_output=True, text=True,
        timeout=10, check=False,
    )
    output = result.stdout + result.stderr
    if result.returncode != expected or text not in output:
        raise AssertionError(
            f"{arguments!r}: expected exit {expected} with {text!r}; "
            f"got {result.returncode}: {output}"
        )


def entry_offset(image):
    if len(image) < 32 or struct.unpack_from("<I", image)[0] != 0xFEEDFACF:
        raise ValueError("expected original ARM64 Mach-O fixture")
    count, length = struct.unpack_from("<II", image, 16)
    end, position = 32 + length, 32
    if end > len(image):
        raise ValueError("truncated load commands")
    for _ in range(count):
        if position + 8 > end:
            raise ValueError("truncated load command")
        command, size = struct.unpack_from("<II", image, position)
        if size < 8 or position + size > end:
            raise ValueError("invalid load command")
        if command == 0x80000028:
            if size < 24:
                raise ValueError("truncated LC_MAIN")
            offset = struct.unpack_from("<Q", image, position + 8)[0]
            if offset + 12 > len(image):
                raise ValueError("entry outside fixture")
            return offset
        position += size
    raise ValueError("missing LC_MAIN")


def main():
    if len(sys.argv) not in (4, 6, 7):
        raise SystemExit("expected ios27, trusted loader, phase19a fixture and optional services helper/fixture")
    executable, loader, fixture = map(pathlib.Path, sys.argv[1:4])
    native = platform.machine().lower() in {"aarch64", "arm64"}
    check([executable], 2, "usage: ios27")
    check([executable, "run-exec"], 2, "trusted-ios27-loader")
    with tempfile.TemporaryDirectory(prefix="ios27 CLI ") as directory:
        directory = pathlib.Path(directory)
        malformed = directory / "malformed image"
        malformed.write_bytes(b"this is not a Mach-O image")
        missing = directory / "missing image"
        fifo = directory / "FIFO is not an image or trusted loader"
        os.mkfifo(fifo)
        for command, prefix, mode in (
            ("run-contained", [], "contained"),
            ("run-exec", [loader], "exec"),
        ):
            base = [executable, command, *prefix]
            # Refusal must precede opening or parsing an image.
            check([*base, missing], 2, "explicit --unsafe flag")
            check([*base, fixture, "--wrong"], 2, "explicit --unsafe flag")
            check([*base, fixture, "--unsafe", "extra"], 2, "requires a value")
            check([*base, fixture, "--unsafe", "--unknown", "value"],
                  2, "unknown process option")
            for invalid in ("", "0", "60001", "-1", "+1", "1x", " 1", "1.5",
                            "18446744073709551616"):
                check([*base, missing, "--unsafe", "--timeout-ms", invalid],
                      2, "one decimal value from 1 through 60000")
            check([*base, missing, "--unsafe", "--timeout-ms", "1",
                   "--timeout-ms", "2"], 2, "one decimal value")
            for valid in ("1", "60000"):
                check([*base, missing, "--unsafe", "--timeout-ms", valid],
                      1, "could not read file")
            for option in ("--arg", "--env", "--apple"):
                check([*base, missing, "--unsafe", option], 2, "requires a value")
                check([*base, missing, "--unsafe", option, "x" * 4095],
                      1, "could not read file")
                check([*base, missing, "--unsafe", option, "x" * 4096],
                      2, "exceed vector/string/payload limits")
                check([*base, missing, "--unsafe", *([option, ""] * 128)],
                      1, "could not read file")
                check([*base, missing, "--unsafe", *([option, ""] * 129)],
                      2, "exceed vector/string/payload limits")
            # The 64 KiB string budget is shared across all three vectors.
            exact = ["--arg", "x" * 4095] * 16
            check([*base, missing, "--unsafe", *exact], 1, "could not read file")
            check([*base, missing, "--unsafe", *exact, "--env", ""],
                  2, "exceed vector/string/payload limits")
            check([*base, missing, "--unsafe"], 1, "could not read file")
            check([*base, fifo, "--unsafe"], 1, "could not read file")
            check([*base, directory, "--unsafe"], 1, "could not read file")
            check([*base, malformed, "--unsafe"], 1, "mach-o rejected")
            if not native:
                check([*base, fixture, "--unsafe"], 1, "ARM64")
                continue
            check([*base, fixture, "--unsafe"], 0, f"{mode} ARM64 entry returned 101")
            startup = ["--arg", "ios27", "--arg", "oak", "--arg", "pine",
                       "--arg", "cedar", "--env", "MODE=phase19a", "--env",
                       "REPEAT=1", "--apple", "executable_path=/ios27/phase19a",
                       "--apple", "ios27.mode=native"]
            # Run the unchanged full workload twice: recursive stack use,
            # every startup vector, writable globals and BSS must be fresh.
            for _ in range(2):
                check([*base, fixture, "--unsafe", *startup], 0, "entry returned 42")
            check([*base, fixture, "--unsafe", "--arg", "ios27"],
                  0, "entry returned 102")
            original = fixture.read_bytes()
            offset = entry_offset(original)
            # MOV W0,#300; RET checks full-width reporting and CLI success.
            cases = (
                ("return300", [0x52802580, 0xD65F03C0], 0,
                 f"{mode} ARM64 entry returned 300"),
                ("trap", [0xD4200000], 128 + signal.SIGTRAP,
                 f"terminated by signal {signal.SIGTRAP}"),
                ("loop", [0x14000000], 124, "exceeded 1000 ms"),
                ("syscall", [0xD2801588, 0xD4000001, 0xD65F03C0],
                 128 + signal.SIGSYS, f"terminated by signal {signal.SIGSYS}"),
            )
            for name, instructions, code, message in cases:
                mutated = bytearray(original)
                for index, instruction in enumerate(instructions):
                    struct.pack_into("<I", mutated, offset + index * 4, instruction)
                path = directory / f"synthetic {name} fixture"
                path.write_bytes(mutated)
                check([*base, path, "--unsafe"], code, message)
                if name == "loop":
                    check([*base, path, "--unsafe", "--timeout-ms", "50"],
                          124, "exceeded 50 ms")
            check([*base, fixture, "--unsafe"], 0, "entry returned 101")
        if native:
            check([executable, "run-exec", missing, fixture, "--unsafe"],
                  1, "cannot execute exec fixture")
            check([executable, "run-exec", fixture, fixture, "--unsafe"],
                  1, "cannot execute exec fixture")
            check([executable, "run-exec", fifo, fixture, "--unsafe"],
                  1, "guest loader is not a regular file")
            check([executable, "run-exec", directory, fixture, "--unsafe"],
                  1, "guest loader is not a regular file")
        if len(sys.argv) >= 6:
            service_loader, service_fixture = map(pathlib.Path, sys.argv[4:6])
            service = [executable, "run-exec", service_loader, service_fixture]
            check(service, 2, "explicit --unsafe flag")
            check([service_loader], 2, "started only by ios27_run_executable_exec")
            if native:
                check([*service, "--unsafe"], 0, "entry returned 42")
                check([*service, "--unsafe", "--arg", "service-fixture",
                       "--arg", "vectors", "--env", "MODE=guest",
                       "--apple", "service=explicit"], 0, "entry returned 42")
                check([executable, "run-exec", loader, service_fixture, "--unsafe"],
                      0, "entry returned 201")
                for mode, code, message in (
                    ("trap", 128 + signal.SIGTRAP, f"terminated by signal {signal.SIGTRAP}"),
                    ("raw-syscall", 128 + signal.SIGSYS, f"terminated by signal {signal.SIGSYS}"),
                    ("loop", 124, "exceeded 100 ms"),
                ):
                    check([*service, "--unsafe", "--arg", mode,
                           "--timeout-ms", "100"], code, message)
                    check([*service, "--unsafe"], 0, "entry returned 42")
            else:
                check([*service, "--unsafe"], 1, "ARM64")
            if len(sys.argv) == 7:
                shim = pathlib.Path(sys.argv[6])
                command = [executable, "run-exec", service_loader, shim, "--unsafe"]
                if native:
                    check(command, 0, "entry returned 42")
                    check([*command, "--arg", "service-fixture", "--arg", "vectors",
                           "--env", "MODE=guest", "--apple", "service=explicit"],
                          0, "entry returned 42")
                    check([executable, "run-exec", loader, shim, "--unsafe"],
                          0, "entry returned 201")
                else:
                    check(command, 1, "ARM64")
    print("process CLI: parsing, explicit execution opt-in, input rejection and "
          + ("native outcomes/recovery passed" if native else
             "x86_64 refusal passed; native outcomes require ARM64"))


if __name__ == "__main__":
    main()
