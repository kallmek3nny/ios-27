#!/usr/bin/env python3
"""Negative fixture suite for phase21j evidence attestation (no network)."""
from copy import deepcopy
from pathlib import Path
import os
import platform
import runpy

# The negative fixture suite needs a real expected head to reject another
# syntactically valid SHA, even when launched outside GitHub Actions.
os.environ.setdefault("PHASE21J_HEAD_SHA", "f" * 40)
ROOT = Path(__file__).resolve().parents[1]
module = runpy.run_path(str(ROOT / "tools/phase21j-verify-evidence.py"),
                        run_name="phase21j_embedded_tests")
verify = module["verify"]
names = {
    path.stem[len("phase21j-"):]
    for path in (ROOT / "tests").glob("phase21j-*.c")
} | {"trace-cli"}


def expected_rejection(value, message):
    try:
        verify(value)
    except ValueError:
        return
    raise RuntimeError("incorrectly accepted evidence mutation: " + message)


def valid_fixture(mode):
    optimizations = ("-O0", "-O2") if mode == "default" else (
        ("-O1",) if mode == "sanitizer" else ("-O2",)
    )
    tests = [
        {"name": name, "optimization": opt,
         "iterations": 8 if mode == "stress" else 1,
         "elapsed_s": 0.1, "outcome": "passed"}
        for name in sorted(names)
        for opt in optimizations
    ]
    flags = ["-std=c11", "-Wall", "-Wextra", "-Wpedantic",
             "-Werror", "-pthread"]
    if mode == "sanitizer":
        flags.append("-fsanitize=address,undefined")
    return {
        "schema": 1, "arch": platform.machine().lower(),
        "compiler": "cc", "cflags": flags, "mode": mode,
        "head_sha": os.environ.get("PHASE21J_HEAD_SHA", "f" * 40),
        "tests": tests, "elapsed_s": 10.0,
    }


def test_mode(mode):
    good = valid_fixture(mode)
    total, seconds = verify(good)
    if total != len(good["tests"]) or seconds != 10.0:
        raise RuntimeError("valid fixture did not preserve its summary")
    corruptions = []
    missing = deepcopy(good)
    missing["tests"].pop()
    corruptions.append(("missing required test cell", missing))
    duplicate = deepcopy(good)
    duplicate["tests"].append(deepcopy(duplicate["tests"][0]))
    corruptions.append(("duplicate test cell", duplicate))
    skipped = deepcopy(good)
    skipped["tests"][0]["outcome"] = "skipped"
    corruptions.append(("skipped test", skipped))
    iterations = deepcopy(good)
    iterations["tests"][0]["iterations"] = 1 if mode == "stress" else 8
    corruptions.append(("wrong iteration count", iterations))
    weaker = deepcopy(good)
    weaker["cflags"].remove("-Werror")
    corruptions.append(("disabled warnings-as-errors", weaker))
    wrong_arch = deepcopy(good)
    wrong_arch["arch"] = "mips64"
    corruptions.append(("wrong host architecture", wrong_arch))
    wrong_sha = deepcopy(good)
    wrong_sha["head_sha"] = "not-a-head-sha"
    corruptions.append(("unverified revision", wrong_sha))
    wrong_but_valid_sha = deepcopy(good)
    wrong_but_valid_sha["head_sha"] = (
        "e" * 40 if good["head_sha"] != "e" * 40 else "d" * 40
    )
    corruptions.append(("valid-format but mismatched frozen SHA",
                        wrong_but_valid_sha))
    negative_elapsed = deepcopy(good)
    negative_elapsed["tests"][0]["elapsed_s"] = -3.0
    corruptions.append(("negative duration", negative_elapsed))
    impossible_elapsed = deepcopy(good)
    impossible_elapsed["elapsed_s"] = 0
    corruptions.append(("impossible duration sum", impossible_elapsed))
    malformed = deepcopy(good)
    malformed["unexpected"] = 17
    corruptions.append(("unknown evidence field", malformed))
    for label, altered in corruptions:
        expected_rejection(altered, label)
    return len(corruptions)


def main():
    checks = sum(test_mode(mode) for mode in ("default", "sanitizer", "stress"))
    print("phase21j evidence validator: {} malformed/skipped/forged fixture cases rejected; PASS".format(
        checks))


if __name__ == "__main__":
    main()
