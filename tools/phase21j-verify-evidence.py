#!/usr/bin/env python3
"""Bounded independent integrity checks for phase21j native CI evidence."""
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
MAX_BYTES = 1 << 20
REQUIRED = {
    "schema", "arch", "compiler", "cflags", "mode",
    "head_sha", "tests", "elapsed_s",
}
TEST_FIELDS = {"name", "optimization", "iterations", "elapsed_s", "outcome"}


def valid_duration(value):
    return type(value) in (int, float) and 0 <= value <= 360


def verify(report):
    if type(report) is not dict or set(report) != REQUIRED:
        raise ValueError("invalid evidence schema")
    if report["schema"] != 1 or type(report["schema"]) is not int:
        raise ValueError("unsupported evidence schema")
    if report["arch"] != platform.machine().lower():
        raise ValueError("evidence machine does not match native runner")
    if type(report["compiler"]) is not str or not report["compiler"]:
        raise ValueError("missing real compiler")
    if type(report["cflags"]) is not list or not all(
        type(flag) is str for flag in report["cflags"]
    ):
        raise ValueError("invalid compile flag list")
    for required in ("-Werror", "-std=c11", "-Wall", "-Wextra",
                     "-Wpedantic", "-pthread"):
        if required not in report["cflags"]:
            raise ValueError("missing strict compiler flag " + required)
    mode = report["mode"]
    if mode not in ("default", "sanitizer", "stress"):
        raise ValueError("invalid suite mode")
    if mode == "sanitizer" and "-fsanitize=address,undefined" not in report["cflags"]:
        raise ValueError("unverified sanitizer flags")
    if mode != "sanitizer" and "-fsanitize=address,undefined" in report["cflags"]:
        raise ValueError("mode does not match sanitizer flags")
    expected_head = os.environ.get("PHASE21J_HEAD_SHA")
    actual_head = report["head_sha"]
    if (type(actual_head) is not str or
            not re.fullmatch(r"[0-9a-f]{40}", actual_head)):
        raise ValueError("missing frozen git head sha")
    if expected_head and actual_head != expected_head:
        raise ValueError("evidence head sha is not the checked-out PR head")
    expected_names = {
        path.stem[len("phase21j-"):]
        for path in (ROOT / "tests").glob("phase21j-*.c")
    } | {"trace-cli"}
    tests = report["tests"]
    if type(tests) is not list:
        raise ValueError("test outcomes must be a list")
    permitted_optimizations = ("-O0", "-O2") if mode == "default" else (
        ("-O1",) if mode == "sanitizer" else ("-O2",)
    )
    expected = {
        (name, optimization)
        for name in expected_names
        for optimization in permitted_optimizations
    }
    observed = set()
    elapsed = 0.0
    for item in tests:
        if type(item) is not dict or set(item) != TEST_FIELDS:
            raise ValueError("malformed individual test evidence")
        name = item["name"]
        optimization = item["optimization"]
        if type(name) is not str or type(optimization) is not str:
            raise ValueError("invalid test identity")
        identity = (name, optimization)
        if identity not in expected or identity in observed:
            raise ValueError("unlisted or duplicate test case " + name)
        observed.add(identity)
        if item["outcome"] != "passed" or item["iterations"] != (
            8 if mode == "stress" else 1
        ):
            raise ValueError("failed, skipped or under-exercised case")
        duration = item["elapsed_s"]
        if not valid_duration(duration):
            raise ValueError("invalid test elapsed time")
        elapsed += duration
    if observed != expected:
        raise ValueError("test inventory not fully exercised")
    total = report["elapsed_s"]
    if not valid_duration(total) or elapsed > total + len(tests) * 0.00051:
        raise ValueError("test elapsed durations do not reconcile")
    return len(tests), total


def main():
    if len(sys.argv) != 2:
        raise ValueError("usage: phase21j-verify-evidence.py EVIDENCE_JSON")
    path = Path(sys.argv[1])
    if path.stat().st_size > MAX_BYTES:
        raise ValueError("evidence exceeds 1 MiB")
    report = json.loads(path.read_text(encoding="utf-8"))
    expected_head = os.environ.get("PHASE21J_HEAD_SHA")
    if expected_head:
        actual = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=ROOT,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            check=False, timeout=30,
        )
        if actual.returncode or actual.stdout.strip() != expected_head:
            raise ValueError("tested git commit does not match frozen SHA")
    count, elapsed = verify(report)
    print("phase21j evidence: {} compiled test cells, {:.3f}s actual runner elapsed; PASS".format(
        count, elapsed))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, TypeError, KeyError,
            subprocess.TimeoutExpired) as error:
        sys.exit("phase21j evidence: rejected: {}".format(error))
