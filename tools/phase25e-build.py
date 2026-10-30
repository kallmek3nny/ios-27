#!/usr/bin/env python3
"""Build nine real compiler-linked Darwin ObjC2 images without an Apple SDK.

Bound every subprocess; deliberately do not start unsupported guest code.
"""
from __future__ import annotations
import argparse
import importlib.util
import json
import os
from pathlib import Path
import platform
import resource
import shutil
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "tests/phase25e-fixtures.m"
LIBRARIES = ("root", "derived", "worker", "category", "ledger", "pipeline")
APPLICATIONS = ("basic", "protocol", "lifecycle")
DEPS = {
    "root": (), "derived": ("root",), "worker": ("root",),
    "category": ("root",), "ledger": ("root",),
    "pipeline": ("root", "derived", "worker", "category", "ledger"),
    **{"app_" + n: LIBRARIES for n in APPLICATIONS},
}
EXPORTS = {
    "root": ("_OBJC_CLASS_$_Phase25eRoot", "_OBJC_METACLASS_$_Phase25eRoot",
             "_phase25e_load_root", "_phase25e_init_root"),
    "derived": ("_OBJC_CLASS_$_Phase25eDerived", "_OBJC_METACLASS_$_Phase25eDerived",
                "_phase25e_load_derived"),
    "worker": ("_OBJC_CLASS_$_Phase25eWorker", "_OBJC_METACLASS_$_Phase25eWorker"),
    "category": (),
    "ledger": ("_OBJC_CLASS_$_Phase25eLedger", "_OBJC_METACLASS_$_Phase25eLedger",
               "_phase25e_ledger_total"),
    "pipeline": ("_OBJC_CLASS_$_Phase25ePipeline", "_OBJC_METACLASS_$_Phase25ePipeline"),
}


def command(args: list[str], timeout: int = 30) -> str:
    # No shell and no polling; this timeout covers the entire child process.
    try:
        p = subprocess.run([str(a) for a in args], text=True,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           check=False, timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError("timeout: " + str(args[:2])) from exc
    if p.returncode:
        raise RuntimeError("command failed: " + " ".join(map(str, args)) +
                           "\n" + p.stdout[-2400:])
    return p.stdout


def link_object_without_platform(obj: Path) -> Path:
    """lld 17 macOS platform quirk; patch only copied LC_BUILD_VERSION.
    Do not touch compiled instructions, symbols or ObjC2 metadata.
    """
    b = bytearray(obj.read_bytes())
    if len(b) < 32 or struct.unpack_from("<III", b)[:2] != (0xFEEDFACF, 0x0100000C):
        raise ValueError("not a compiler-generated ARM64 Mach-O object")
    count, size = struct.unpack_from("<II", b, 16)
    cursor, end = 32, 32 + size
    if end > len(b) or count > 256:
        raise ValueError("invalid object load commands")
    for _ in range(count):
        if cursor + 8 > end:
            raise ValueError("truncated object load command")
        op, length = struct.unpack_from("<II", b, cursor)
        if length < 8 or length % 8 or cursor + length > end:
            raise ValueError("invalid object command")
        if op in (0x24, 0x32):
            b[cursor:end - length] = b[cursor + length:end]
            b[end - length:end] = bytes(length)
            struct.pack_into("<II", b, 16, count - 1, size - length)
            patched = obj.with_suffix(".link.o")
            patched.write_bytes(b)
            return patched
        cursor += length
    raise ValueError("missing compiler-generated platform load command")


def make_tbd(out: Path, name: str) -> Path:
    path = out / ("lib" + name + ".tbd")
    exports = EXPORTS[name]
    path.write_text("--- !tapi-tbd-v3\narchs: [ arm64 ]\n"
                    "platform: macosx\n"
                    "install-name: '@rpath/lib" + name + ".dylib'\n"
                    "current-version: 1.0.0\ncompatibility-version: 1.0.0\n"
                    "exports:\n  - archs: [ arm64 ]\n    symbols: [ " +
                    ", ".join("'" + e + "'" for e in exports) + " ]\n...\n",
                    encoding="ascii")
    return path


def link_images(out: Path, clang: str, linker: str) -> dict[str, Path]:
    outputs = {}
    for name in LIBRARIES:
        make_tbd(out, name)
    for name in (*LIBRARIES, *("app_" + n for n in APPLICATIONS)):
        obj = out / (name + ".o")
        macro = "PHASE25E_" + name.upper()
        command([clang, "--target=arm64-apple-darwin", "-x", "objective-c",
                 "-std=gnu11", "-fobjc-runtime=macosx-13.0",
                 "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                 "-ffreestanding", "-fPIC", "-fno-builtin",
                 "-fno-stack-protector", "-fno-unwind-tables",
                 "-fno-asynchronous-unwind-tables", "-O1", "-D" + macro,
                 "-c", SRC, "-o", obj])
        linked_obj = link_object_without_platform(obj)
        app = name.startswith("app_")
        output = out / (name if app else "lib" + name + ".dylib")
        options = (["-e", "_main"] if app else
                   ["-dylib", "-install_name", "@rpath/lib" + name + ".dylib"])
        command([linker, "-arch", "arm64", "-platform_version",
                 "macos", "13.0", "13.0", *options,
                 "-undefined", "dynamic_lookup",
                 "-rpath", "@loader_path", "-o", output, linked_obj,
                 *(out / ("lib" + x + ".tbd") for x in DEPS[name])])
        outputs[output.name] = output
    return outputs


def load_inspector():
    path = ROOT / "tools/phase25e-inspect.py"
    spec = importlib.util.spec_from_file_location("phase25e_inspect", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("missing independently owned inspection module")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def validate_catalog(inspector, images: dict) -> list:
    expected = {"lib" + n + ".dylib" for n in LIBRARIES}
    expected |= {"app_" + n for n in APPLICATIONS}
    if set(images) != expected:
        raise ValueError("nine independently linked artifacts required")
    for name, img in images.items():
        app = name.startswith("app_")
        expected_deps = {"@rpath/lib" + x + ".dylib" for x in
                         DEPS[name if app else name[3:-6]]}
        if set(img.dependencies) != expected_deps:
            raise ValueError("missing/crossed linked dependency " + name +
                             ": " + str(img.dependencies))
        required = ("__objc_classrefs", "__objc_selrefs") if app else (
            ("__objc_catlist",) if name == "libcategory.dylib" else
            ("__objc_classlist",))
        if not all(s in img.objc_sections for s in required):
            raise ValueError("missing compiler-produced ObjC metadata: " + name)
        if img.kind != ("MH_EXECUTE" if app else "MH_DYLIB"):
            raise ValueError("wrong linked image type: " + name)
        if app and img.entry_offset is None:
            raise ValueError("app missing entry offset")
        if not app and img.entry_offset is not None:
            raise ValueError("unexpected dylib entry offset")
    return inspector.dependency_order(images, "app_basic")


def faults(inspector, outputs: dict, images: dict) -> dict:
    original = outputs["app_basic"].read_bytes()
    results = {}
    injections = {
        "truncated_image": original[:12],
        "wrong_cpu": original[:4] + bytes(4) + original[8:],
        "invalid_load_command": original[:36] + bytes(4) + original[40:],
        "missing_lc_main": mutate_lc_main(original),
        "invalid_objc_section": corrupt_objc_section(original),
    }
    for key, corrupt in injections.items():
        try:
            inspector.inspect_blob(corrupt, key)
            raise AssertionError("fail-open image: " + key)
        except inspector.InspectionFailure as exc:
            results[key] = exc.category + ": " + exc.detail
    try:
        inspector.dependency_order({k: v for k, v in images.items()
                                    if k != "libroot.dylib"}, "app_basic")
        raise AssertionError("missing library resolved")
    except inspector.InspectionFailure as exc:
        results["missing_dylib"] = exc.category + ": " + exc.detail
    for key, vectors in {
        "bad_argv": (("app", "\0bad"), ("MODE=phase25e",), ("exec=/App/phase25e",)),
        "bad_env": (("app", "basic"), ("BROKEN",), ("exec=/App/phase25e",)),
    }.items():
        try:
            inspector.validate_startup(*vectors)
            raise AssertionError("bad process vectors accepted")
        except inspector.InspectionFailure as exc:
            results[key] = exc.category + ": " + exc.detail
    gate = inspector.execution_gate()
    if gate["darwin_objc_sends_executed"] or gate["ownership_transitions_executed"]:
        raise AssertionError("unsupported native methods counted as executed")
    results["unsupported_objc_dispatch"] = gate["status"]
    results["unsupported_signature"] = "blocked: no verified phase25b signature ABI on main"
    results["unresolved_selector"] = "blocked: no verified phase25a/25b selector binder on main"
    results["premature_teardown"] = "blocked: no verified phase25c/25d lifecycle pin API on main"
    return results


def mutate_lc_main(blob: bytes) -> bytes:
    b = bytearray(blob)
    count = struct.unpack_from("<I", b, 16)[0]
    pos = 32
    for _ in range(count):
        op, length = struct.unpack_from("<II", b, pos)
        if op == 0x80000028:
            struct.pack_into("<Q", b, pos + 8, 0xFFFFFFFFFFFFFFFF)
            return bytes(b)
        pos += length
    raise ValueError("fixture lacks LC_MAIN")


def corrupt_objc_section(blob: bytes) -> bytes:
    b = bytearray(blob)
    count = struct.unpack_from("<I", b, 16)[0]
    pos = 32
    for _ in range(count):
        op, length = struct.unpack_from("<II", b, pos)
        if op == 0x19:
            sections = struct.unpack_from("<I", b, pos + 64)[0]
            for j in range(sections):
                sec = pos + 72 + 80 * j
                name = bytes(b[sec:sec + 16]).split(b"\0")[0]
                if name in (b"__objc_selrefs", b"__objc_classrefs"):
                    struct.pack_into("<I", b, sec + 48, len(blob) - 1)
                    return bytes(b)
        pos += length
    raise ValueError("fixture lacks ObjC pointer metadata")


def run(args):
    start = time.perf_counter()
    out = Path(args.output).resolve()
    out.mkdir(parents=True, exist_ok=True)
    clang = shutil.which("clang")
    ld = next((shutil.which(n) for n in ("ld64.lld", "ld64.lld-19",
                                        "ld64.lld-18", "ld64.lld-17")
               if shutil.which(n)), None)
    if not clang or not ld:
        raise RuntimeError("clang and ld64.lld are required, no fallback executables")
    inspector = load_inspector()
    outputs = link_images(out, clang, ld)
    images = {name: inspector.inspect_file(path) for name, path in outputs.items()}
    load_order = validate_catalog(inspector, images)
    inspector.validate_startup(("phase25e", "basic"), ("MODE=phase25e",),
                               ("exec=/App/phase25e",))
    failures = faults(inspector, outputs, images)
    t0 = time.perf_counter()
    for _ in range(args.rounds):
        repeated = {name: inspector.inspect_file(path)
                    for name, path in outputs.items()}
        if repeated != images or inspector.dependency_order(repeated, "app_basic") != load_order:
            raise AssertionError("nondeterministic inspection")
    inspected_ms = (time.perf_counter() - t0) * 1000.0
    gate = inspector.execution_gate()
    report = {
        "phase": "25e", "host_arch": platform.machine(),
        "compiler": command([clang, "--version"], 10).splitlines()[0],
        "linked_dylibs": len(LIBRARIES), "linked_executables": len(APPLICATIONS),
        "images": [images[n].as_dict() for n in sorted(images)],
        "dependency_first_load_order": load_order,
        "fail_closed_cases": failures,
        "native_execution": gate,
        "expected_only_if_verified": {
            "per_application_native_objc_sends": 200000,
            "per_application_native_ownership_transitions": 150000,
            "actual_verified_application_starts": 0,
        },
        "execution_trace": [
            {"stage": "clang_and_lld", "provenance": "host_tools",
             "guest_instructions_executed": 0},
            {"stage": "mach_o_inspection", "provenance": "host_python_parser",
             "guest_instructions_executed": 0},
            {"stage": "objc_application_entry", "provenance": "not_executed",
             "guest_instructions_executed": 0},
        ],
        "benchmarks": {
            "full_build_and_validation_ms": round((time.perf_counter() - start) * 1000, 3),
            "inspection_rounds": args.rounds,
            "nine_image_inspection_ms": round(inspected_ms / args.rounds, 3),
            "guest_startup_latency_ms": None,
            "guest_dispatch_throughput_per_sec": None,
            "guest_teardown_latency_ms": None,
            "peak_host_maxrss_kib": resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
            "mapped_guest_memory_bytes": 0,
            "live_guest_instances_after_validation": 0,
        },
    }
    path = out / "phase25e-evidence.json"
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf8")
    print(json.dumps({"report": str(path),
                      "linked_dylibs": len(LIBRARIES),
                      "linked_applications": len(APPLICATIONS),
                      "inspection_rounds": args.rounds,
                      "native_sends": gate["darwin_objc_sends_executed"],
                      "native_ownership": gate["ownership_transitions_executed"],
                      "native_status": gate["status"]}, sort_keys=True))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="build/phase25e")
    parser.add_argument("--rounds", type=int, default=100)
    args = parser.parse_args()
    if not 1 <= args.rounds <= 1000:
        parser.error("--rounds must be 1..1000")
    try:
        run(args)
    except (RuntimeError, ValueError, AssertionError, OSError, struct.error) as exc:
        sys.exit("phase25e failed: " + str(exc))


if __name__ == "__main__":
    main()
