#!/usr/bin/env python3
"""Build native ARM64 simulator display/input helpers with verified imports.

Requires the configured Darling cross-build and a local iOS simulator runtime.
Downloads hash-pinned GPL sources. Link stubs contain symbol declarations only;
they are build inputs and are never installed as runtime implementations.
"""

import argparse
from collections import defaultdict
from contextlib import ExitStack
from hashlib import sha256
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import shutil
import struct
import subprocess
import sys
import tempfile
from urllib.request import urlopen


ROOT = Path(__file__).resolve().parent.parent
INTEGRATION = ROOT / "integration/darling/shims"
COMPONENTS = {
    "simfb": ("host/simfb/simfb.c", "SimFramebuffer", 6, []),
    "simhid": ("host/simhid/simhid.m", "SimulatorHID", 6, []),
    "iosurfaced": ("host/iosurfaced/iosurfaced.m", "iosurfaced", 2, ["-include", "xpc/launchd.h"]),
}
PROVIDERS = [
    "/usr/lib/libSystem.B.dylib", "/usr/lib/libobjc.A.dylib",
    "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation",
    "/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics",
    "/System/Library/Frameworks/IOSurface.framework/IOSurface",
    "/System/Library/Frameworks/Foundation.framework/Foundation",
    "/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit",
]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def run(command, log):
    with log.open("w", encoding="utf-8") as output:
        result = subprocess.run(list(map(str, command)), stdout=output,
                                stderr=subprocess.STDOUT, timeout=120)
    require(result.returncode == 0, f"command failed; {log}\n" +
            "\n".join(log.read_text(errors="replace").splitlines()[-25:]))


def imports(obj):
    data = obj.read_bytes()
    require(len(data) >= 32, "truncated compiler object")
    magic, cpu, _, kind, count, size, _, _ = struct.unpack_from("<8I", data)
    require((magic, cpu, kind) == (0xFEEDFACF, 0x100000C, 1) and size <= len(data) - 32,
            "compiler did not produce an ARM64 Mach-O object")
    cursor, names = 32, []
    for _ in range(count):
        require(cursor + 8 <= 32 + size, "truncated object load command")
        cmd, length = struct.unpack_from("<II", data, cursor)
        require(length >= 8 and cursor + length <= 32 + size, "invalid object load command")
        if cmd == 2:
            require(length == 24, "invalid object symbol table command")
            symoff, nsyms, stroff, strsize = struct.unpack_from("<4I", data, cursor + 8)
            require(symoff + nsyms * 16 <= len(data) and stroff + strsize <= len(data),
                    "object symbol table exceeds file")
            for index in range(nsyms):
                name, typ, _, _, value = struct.unpack_from("<IBBHQ", data, symoff + index * 16)
                if typ & 0x0E == 0 and name:
                    require(name < strsize and value == 0, "invalid undefined object symbol")
                    end = data.index(0, stroff + name, stroff + strsize)
                    names.append(data[stroff + name:end].decode("ascii"))
        cursor += length
    require(names, "object has no framework imports")
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--darling-build", type=Path, required=True)
    parser.add_argument("--runtime-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--linker", help="LLVM ld.lld (Darwin flavor is selected explicitly)")
    args = parser.parse_args()
    require(sys.platform == "linux", "use the Linux Darling cross-build environment")
    os.environ["PATH"] = os.pathsep.join(p for p in os.environ.get("PATH", os.defpath).split(os.pathsep)
                                        if p and not p.startswith("/mnt/"))
    build, runtime, output = args.darling_build.resolve(strict=True), args.runtime_root.resolve(strict=True), args.output.resolve()
    config = json.loads((build / "compiler.json").read_text())
    upstream = json.loads((ROOT / "integration/darling/upstream.json").read_text())
    require(config["revision"] == upstream["darling"]["revision"], "unexpected Darling revision")
    sdk = Path("Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk")
    linker = args.linker or shutil.which("ld.lld")
    require(linker, "LLVM ld.lld is required for simulator-platform TAPI declarations")
    # A build owns only a previously empty output directory or one with our marker.
    marker = output / "simulator-shims.json"
    require(not output.exists() or not any(output.iterdir()) or marker.is_file(),
            "output directory contains unrelated files")
    output.mkdir(parents=True, exist_ok=True)
    pinned = json.loads((INTEGRATION / "sources.json").read_text())
    record = {"upstream_revision": pinned["revision"], "sources_sha256": pinned["files"],
              "patch_sha256": sha256((INTEGRATION / "simfb-native.patch").read_bytes()).hexdigest(),
              "native_header_sha256": sha256((INTEGRATION / "linux-arm64.h").read_bytes()).hexdigest(),
              "cpu_translation": False, "built": {}, "frame_output_verified": False}
    marker.write_text(json.dumps(record, indent=2) + "\n")
    spec = importlib.util.spec_from_file_location("ios27_cache_audit", ROOT / "tools/apple-cache-audit.py")
    audit = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = audit
    spec.loader.exec_module(audit)
    with tempfile.TemporaryDirectory(prefix="sources-", dir=output) as temporary, ExitStack() as stack:
        sources = Path(temporary)
        for relative, expected in pinned["files"].items():
            url = "https://raw.githubusercontent.com/x86sim/x86sim/" + pinned["revision"] + "/" + relative
            with urlopen(url, timeout=30) as response:
                data = response.read(2 * 1024 * 1024)
            require(sha256(data).hexdigest() == expected, "source hash mismatch: " + relative)
            path = sources / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        shutil.copyfile(sources / "LICENSE", output / "LICENSE.x86sim")
        run(["git", "-C", sources, "init", "--quiet"], output / "source-init.log")
        run(["git", "-C", sources, "apply", INTEGRATION / "simfb-native.patch"], output / "patch.log")
        cache = audit.Cache(stack, runtime / "System/Library/Caches/com.apple.dyld/dyld_sim_shared_cache_arm64")
        resolver = audit.Resolver(cache, None, {}, lambda path: audit.standalone(stack, runtime, path))
        for component, (relative, name, kind, extra) in COMPONENTS.items():
            obj = output / (component + ".o")
            run([build / "target-cc", "-target", "arm64-apple-ios17.0-simulator", "-isysroot",
                 Path(config["source"]) / sdk, "-isystem", build / sdk / "usr/include", "-I", INTEGRATION,
                 "-fblocks", "-fno-objc-arc", "-O2", "-Wno-nullability-completeness", "-Wno-availability",
                 "-Wno-deprecated-declarations", *extra, "-c", sources / relative, "-o", obj],
                output / (component + "-compile.log"))
            groups = defaultdict(list)
            for symbol in imports(obj):
                for provider in PROVIDERS:
                    if resolver.resolve(provider, symbol):
                        groups[provider].append(symbol)
                        break
                else:
                    raise ValueError(f"{component}: unresolved simulator import {symbol}")
            stubs = []
            for number, (provider, names) in enumerate(groups.items()):
                stub = output / f"{component}-provider-{number}.tbd"
                stub.write_text("--- !tapi-tbd\ntbd-version: 4\ntargets: [ arm64-ios-simulator ]\n"
                                "install-name: " + json.dumps(provider) + "\ncurrent-version: 1.0.0\n"
                                "compatibility-version: 1.0.0\nexports:\n  - targets: [ arm64-ios-simulator ]\n"
                                "    symbols: [ " + ", ".join(map(json.dumps, names)) + " ]\n...\n")
                stubs.append(stub)
            binary = output / name
            options = [] if kind == 2 else ["-dylib", "-install_name",
                "/System/Library/PrivateFrameworks/" + name + ".framework/" + name]
            run([linker, "-flavor", "darwin", *options, "-arch", "arm64", "-platform_version", "ios-simulator",
                 "17.0", "27.0", "-o", binary, obj, *stubs], output / (component + "-link.log"))
            run([build / "host-tools-build/buildtools/adhoc-sign", binary], output / (component + "-sign.log"))
            image = audit.Image(audit.mapped_file(stack, binary))
            dependencies = [entry.path for entry in image.dependencies]
            require(image.kind == kind and image.platform == 7 and set(dependencies) == set(groups),
                    "unexpected simulator image metadata: " + name)
            record["built"][name] = {"sha256": sha256(binary.read_bytes()).hexdigest(),
                                      "imports_verified": sum(map(len, groups.values())),
                                      "dependency_install_names": dependencies, "executed": False}
            if kind == 6:
                bundle = output / (name + ".framework")
                bundle.mkdir(exist_ok=True)
                shutil.copyfile(binary, bundle / name)
                info = {"CFBundleExecutable": name, "CFBundleIdentifier": "org.ios27." + name,
                        "CFBundleInfoDictionaryVersion": "6.0", "CFBundleName": name,
                        "CFBundlePackageType": "FMWK", "CFBundleSupportedPlatforms": ["iPhoneSimulator"],
                        "CFBundleVersion": "1"}
                if component == "simhid":
                    info["NSPrincipalClass"] = "SimHIDSystem"
                metadata = plistlib.dumps(info)
                (bundle / "Info.plist").write_bytes(metadata)
                record["built"][name].update(bundle=bundle.name,
                    info_plist_sha256=sha256(metadata).hexdigest())
            marker.write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        sys.exit("simulator shim build: " + str(error))
