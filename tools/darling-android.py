#!/usr/bin/env python3
"""Prepare/check an ARM64 Android cross-build of a pinned, patched Darling.

Build tools run on the Linux PC; target ELF files use the Android NDK and guest
Mach-O files use Darling's open-source SDK. This does not install or execute a
Darling runtime and never enables an instruction translator.
"""

import argparse
from hashlib import sha256
import json
import os
from pathlib import Path
import re
import shutil
import signal
from struct import unpack_from
import subprocess
import sys
from urllib.parse import urlsplit
from urllib.request import urlopen


REVISION = "e41ffd0d9799d740d85e0e90812d6ed25df51200"
WRAPPER = '''#!/usr/bin/env python3
import json, os, pathlib, sys
config = json.loads(pathlib.Path(__file__).with_name("compiler.json").read_text())
args = sys.argv[1:]
targets = []
for i, arg in enumerate(args):
    if arg in ("-target", "--target") and i + 1 < len(args):
        targets.append(args[i + 1])
    elif arg.startswith(("--target=", "-target=")):
        targets.append(arg.split("=", 1)[1])
cxx = pathlib.Path(__file__).name.endswith("cxx")
compiler = config["cxx" if cxx else "cc"]
if not any("apple" in target for target in targets):
    args = ["--target=aarch64-linux-android" + str(config["api"]),
            "--sysroot=" + config["sysroot"], "-resource-dir=" + config["resource"],
            "-fuse-ld=lld", "--rtlib=compiler-rt"] + args
    if cxx:
        args = ["-stdlib=libc++", "-static-libstdc++"] + args
    if config["dependencies"] and not any(a in args for a in ("-c", "-S", "-E", "-M", "-MM", "-fsyntax-only")):
        libdir = str(pathlib.Path(config["dependencies"]) / "lib")
        args = ["-L" + libdir, "-Wl,-rpath-link," + libdir] + args
os.execv(compiler, [compiler] + args)
'''
HOST_WRAPPER = '''#!/usr/bin/env python3
import json, os, pathlib, sys
config = json.loads(pathlib.Path(__file__).with_name("compiler.json").read_text())
compiler = config["cxx" if pathlib.Path(__file__).name.endswith("cxx") else "cc"]
args = sys.argv[1:]
if config["host_prefix"]:
    include = pathlib.Path(config["host_prefix"]) / "include"
    args = ["-isystem", str(include), "-isystem", str(include / "x86_64-linux-gnu")] + args
os.execv(compiler, [compiler] + args)
'''


def require(condition, message):
    if not condition:
        raise ValueError(message)


def source_changes(source):
    def git(directory, *arguments):
        return subprocess.run(["git", "-C", str(directory), *arguments], capture_output=True,
                              text=True, check=True, timeout=30).stdout.strip()

    result = []
    for relative in git(source, "diff", "--name-only", "HEAD", "--ignore-submodules=untracked").splitlines():
        path = source / relative
        if path.is_dir() and (path / ".git").exists():
            entry = git(source, "ls-tree", "HEAD", "--", relative).split()
            require(len(entry) >= 3 and entry[0] == "160000" and git(path, "rev-parse", "HEAD") == entry[2],
                    f"submodule revision changed: {relative}")
            result.extend(relative + "/" + item for item in
                          git(path, "diff", "--name-only", "HEAD", "--ignore-submodules=untracked").splitlines())
        else:
            result.append(relative)
    return set(result)


def run(command, log, timeout):
    with log.open("w", encoding="utf-8") as output:
        with subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT,
                              start_new_session=True) as process:
            try:
                result = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
                raise ValueError(f"command exceeded {timeout}s; process group stopped; {log}") from None
    if result:
        tail = "\n".join(log.read_text(errors="replace").splitlines()[-35:])
        raise ValueError(f"command failed ({result}); {log}\n{tail}")


def compiler_checks(build, config):
    source = build / "probe.c"
    source.write_text("int main(void) { return 0; }\n", encoding="ascii")
    for kind, compiler, flags in (
            ("android", str(build / "target-cc"), []),
            ("darwin", str(build / "target-cc"), ["--target=arm64-apple-macos11", "-ffreestanding"]),
            ("host", str(build / "host-cc"), [])):
        obj = build / f"probe-{kind}.o"
        run([compiler, *flags, "-c", str(source), "-o", str(obj)], build / f"probe-{kind}.log", 60)
        data = obj.read_bytes()
        if kind == "darwin":
            require(unpack_from("<II", data) == (0xFEEDFACF, 0x0100000C), "guest compiler did not produce ARM64 Mach-O")
        else:
            require(data[:6] == b"\x7fELF\x02\x01", f"{kind} compiler did not produce ELF64")
            require(unpack_from("<H", data, 18)[0] == (183 if kind == "android" else 62),
                    f"{kind} compiler produced the wrong architecture")
    run([str(build / "target-cc"), str(source), "-o", str(build / "probe-android")],
        build / "probe-android-link.log", 60)
    cxx = build / "probe.cpp"
    cxx.write_text('#include <string>\nint main() { return std::string("ndk").size() == 3 ? 0 : 1; }\n')
    run([str(build / "target-cxx"), str(cxx), "-o", str(build / "probe-android-cxx")],
        build / "probe-android-cxx-link.log", 60)
    print("compiler routing checked: x86_64 ELF build tools, ARM64 Android ELF, ARM64 Darwin Mach-O")


def inspect_native_outputs(build):
    artifacts = []
    for relative in ("src/startup/darling", "src/startup/mldr/mldr",
                     "src/external/darlingserver/darlingserver"):
        path = build / relative
        data = path.read_bytes()
        require(len(data) >= 64 and data[:6] == b"\x7fELF\x02\x01"
                and unpack_from("<H", data, 18)[0] == 183, f"{relative} is not AArch64 ELF64")
        headers = subprocess.run(["readelf", "-W", "-l", str(path)], capture_output=True,
                                 text=True, check=True, timeout=15).stdout
        require("[Requesting program interpreter: /system/bin/linker64]" in headers,
                f"{relative} does not use the Android dynamic linker")
        dynamic = subprocess.run(["readelf", "-W", "-d", str(path)], capture_output=True,
                                 text=True, check=True, timeout=15).stdout
        needed = re.findall(r"\(NEEDED\).*\[([^\]]+)\]", dynamic)
        require("libc.so" in needed and "libc.so.6" not in needed and all("/" not in name for name in needed),
                f"{relative} has non-portable or non-Android ELF dependencies: {needed}")
        artifacts.append({"path": relative, "sha256": sha256(data).hexdigest(),
                          "bytes": len(data), "needed": needed})
    (build / "native-artifacts.json").write_text(json.dumps(artifacts, indent=2) + "\n")
    print("verified Android linker, ARM64 ELF headers and relocatable dependency names for all three native programs")


def build_named_semaphore(build, config):
    manifest = Path(__file__).resolve().parent.parent / "integration/darling/semaphore-source.json"
    pinned = json.loads(manifest.read_text())
    directory = build / "native-deps"
    directory.mkdir(exist_ok=True)
    source = directory / "semaphore.c"
    if source.is_file():
        content = source.read_bytes()
    else:
        require(urlsplit(pinned["url"]).scheme == "https", "semaphore source requires HTTPS")
        with urlopen(pinned["url"], timeout=30) as response:
            require(urlsplit(response.url).scheme == "https", "source redirect left HTTPS")
            content = response.read(pinned["size"] + 1)
    require(len(content) == pinned["size"] and sha256(content).hexdigest() == pinned["sha256"],
            "semaphore source hash mismatch")
    header = '#include <paths.h>\n#undef _PATH_TMP\n#define _PATH_TMP ' + json.dumps(config["prefix"].rstrip("/") + "/tmp/") + '\n'
    identity = sha256(content + header.encode() + json.dumps(config, sort_keys=True).encode()).hexdigest()
    library = directory / "libandroid-posix-semaphore.so"
    stamp = directory / "semaphore-build.json"
    if stamp.is_file() and library.is_file():
        previous = json.loads(stamp.read_text())
        if previous.get("inputs") == identity and previous.get("sha256") == sha256(library.read_bytes()).hexdigest():
            return library
    source.write_bytes(content)
    config_header = directory / "semaphore-config.h"
    config_header.write_text(header)
    run([str(build / "target-cc"), "-fPIC", "-shared", "-O2", "-include", str(config_header),
         "-Dsem_open=libandroid_sem_open", "-Dsem_close=libandroid_sem_close", "-Dsem_unlink=libandroid_sem_unlink",
         "-Wl,-soname,libandroid-posix-semaphore.so", str(source), "-o", str(library)],
        directory / "semaphore-build.log", 60)
    stamp.write_text(json.dumps({"inputs": identity, "sha256": sha256(library.read_bytes()).hexdigest(),
                                 "source_sha256": pinned["sha256"], "temporary_directory": config["prefix"] + "/tmp"},
                                indent=2) + "\n")
    return library


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--ndk", required=True, type=Path)
    parser.add_argument("--dependencies", type=Path, help="native Android dependency prefix from darling-android-deps.py")
    parser.add_argument("--host-prefix", type=Path, help="optional private prefix containing Linux build tools and development libraries")
    parser.add_argument("--api", type=int, default=30)
    parser.add_argument("--sigrtmin", required=True, type=int, help="value measured on target Android libc")
    parser.add_argument("--sigrtmax", required=True, type=int)
    parser.add_argument("--prefix", default="/data/local/tmp/ios27-darling")
    parser.add_argument("--phase", choices=("check", "configure", "build"), default="check")
    parser.add_argument("--host-only", action="store_true", help="build only launcher, mldr and darlingserver")
    parser.add_argument("--timeout", type=int, default=600)
    args = parser.parse_args()
    require(sys.platform == "linux" and os.uname().machine == "x86_64", "this cross-build recipe requires an x86_64 Linux build host")
    # WSL imports the Windows PATH. CMake's repeated searches across those
    # mounts can take minutes, and none of those tools can build Linux hosts.
    os.environ["PATH"] = os.pathsep.join(dict.fromkeys(
        entry for entry in os.environ.get("PATH", os.defpath).split(os.pathsep)
        if entry and not entry.startswith("/mnt/")))
    os.environ["CMAKE_BUILD_PARALLEL_LEVEL"] = "2"
    require(30 <= args.api <= 37 and 1 <= args.sigrtmin <= args.sigrtmax <= 128 and args.timeout > 0,
            "invalid target API/signal range/timeout")
    source, build, ndk = args.source.resolve(strict=True), args.build.resolve(), args.ndk.resolve(strict=True)
    dependencies = args.dependencies.resolve(strict=True) if args.dependencies else None
    host_prefix = args.host_prefix.resolve(strict=True) if args.host_prefix else None
    require(args.phase == "check" or dependencies is not None, "configure/build requires --dependencies")
    if host_prefix:
        os.environ["PATH"] = str(host_prefix / "bin") + os.pathsep + os.environ["PATH"]
        os.environ["CMAKE_PREFIX_PATH"] = str(host_prefix)
        os.environ["LD_LIBRARY_PATH"] = str(host_prefix / "lib/x86_64-linux-gnu") + (
            os.pathsep + os.environ["LD_LIBRARY_PATH"] if os.environ.get("LD_LIBRARY_PATH") else "")
        os.environ["BISON_PKGDATADIR"] = str(host_prefix / "share/bison")
        os.environ["M4"] = str(host_prefix / "bin/m4")
    revision = subprocess.run(["git", "-C", str(source), "rev-parse", "HEAD"],
                              capture_output=True, text=True, check=True, timeout=15).stdout.strip()
    require(revision == REVISION, "Darling checkout does not match the pinned revision")
    patch_file = Path(__file__).resolve().parent.parent / "integration/darling/source-patches.json"
    patches = json.loads(patch_file.read_text())
    require(patches["base_revision"] == revision, "source patch set is for a different revision")
    require(source_changes(source) <= set(patches["files"]),
            "Darling checkout or submodule has unrecognized tracked modifications")
    for relative, digest in patches["files"].items():
        require(sha256((source / relative).read_bytes()).hexdigest() == digest,
                f"apply the maintained source patch set first; {relative} does not match")
    require(build != source and not build.is_relative_to(source), "use a separate build directory")
    prebuilt = ndk / "toolchains/llvm/prebuilt"
    hosts = [prebuilt / tag for tag in ("linux-x86_64", "windows-x86_64") if (prebuilt / tag / "sysroot").is_dir()]
    require(hosts, "NDK sysroot not found")
    host = hosts[0]
    resources = sorted((host / "lib/clang").glob("*"), key=lambda p: p.name)
    resources = [p for p in resources if (p / "include/stddef.h").is_file()]
    require(resources, "NDK compiler resource headers not found")
    cc, cxx = shutil.which("clang"), shutil.which("clang++")
    require(cc and cxx and shutil.which("ld.lld"), "Linux clang/clang++/ld.lld are required")
    config = {"source": str(source), "revision": revision, "api": args.api,
              "sysroot": str(host / "sysroot"), "resource": str(resources[-1]), "cc": cc, "cxx": cxx,
              "sigrtmin": args.sigrtmin, "sigrtmax": args.sigrtmax, "prefix": args.prefix,
              "dependencies": str(dependencies) if dependencies else None,
              "host_prefix": str(host_prefix) if host_prefix else None,
              "source_patches_sha256": sha256(patch_file.read_bytes()).hexdigest(),
              "host_wrapper_sha256": sha256(HOST_WRAPPER.encode()).hexdigest(),
              "wrapper_sha256": sha256(WRAPPER.encode()).hexdigest()}
    cookie = build / "compiler.json"
    if build.exists() and any(build.iterdir()):
        previous = json.loads(cookie.read_text()) if cookie.is_file() else {}
        # CMake tracks source changes and regenerates their build rules. A
        # maintained source patch update may reuse a build with the same ABI
        # and compilers; changing the toolchain still requires a fresh build.
        previous.pop("source_patches_sha256", None)
        toolchain = {key: value for key, value in config.items() if key != "source_patches_sha256"}
        # Reconfiguring an install location is handled by CMake's normal
        # dependency rebuilds. Other phases must use the configured prefix.
        if args.phase == "configure":
            previous.pop("prefix", None)
            toolchain.pop("prefix", None)
        require(previous == toolchain,
                "build directory belongs to another configuration")
    build.mkdir(parents=True, exist_ok=True)
    cookie.write_text(json.dumps(config, indent=2) + "\n")
    for name in ("target-cc", "target-cxx", "host-cc", "host-cxx"):
        wrapper = build / name
        wrapper.write_text(HOST_WRAPPER if name.startswith("host-") else WRAPPER, encoding="utf-8")
        wrapper.chmod(0o700)
    compiler_checks(build, config)
    if args.phase == "check":
        return
    semaphore = build_named_semaphore(build, config)
    if args.phase == "configure":
        command = ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                   "-DCMAKE_SYSTEM_PROCESSOR=aarch64", "-DDARLING_CROSS_BUILD=ON", "-DDARLING_HOST_ANDROID=ON",
                   f"-DCMAKE_C_COMPILER={build / 'target-cc'}", f"-DCMAKE_CXX_COMPILER={build / 'target-cxx'}",
                   f"-DCMAKE_ASM_COMPILER={build / 'target-cc'}",
                   f"-DCMAKE_ASM-ATT_COMPILER={build / 'target-cc'}", "-DCMAKE_ASM-ATT_COMPILER_ARG1=-c",
                   f"-DHOST_CC={build / 'host-cc'}", f"-DHOST_CXX={build / 'host-cxx'}",
                   "-DTARGET_ARM64=ON", "-DTARGET_x86_64=OFF",
                   "-DTARGET_i386=OFF", "-DCOMPONENTS=core", "-DENABLE_METAL=OFF", "-DCOMPILE_PY2_BYTECODE=OFF",
                   "-DDARLING_NO_CCACHE=ON", "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_INSTALL_PREFIX={args.prefix}",
                   f"-DDARLING_ANDROID_TMPDIR={args.prefix}/tmp", f"-DLINUX_SIGRTMIN={args.sigrtmin}",
                   f"-DLINUX_SIGRTMAX={args.sigrtmax}"]
        command += [f"-DCMAKE_PREFIX_PATH={dependencies}",
                    f"-DCMAKE_FIND_ROOT_PATH={dependencies};{host / 'sysroot'}",
                    "-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER", "-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY",
                    "-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY",
                    f"-DOPENSSL_INCLUDE_DIR={dependencies / 'include'}",
                    f"-DOPENSSL_CRYPTO_LIBRARY={dependencies / 'lib/libcrypto.so'}",
                    f"-DOPENSSL_SSL_LIBRARY={dependencies / 'lib/libssl.so'}",
                    f"-DLIBXML2_INCLUDE_DIR={dependencies / 'include/libxml2'}",
                    f"-DLIBXML2_LIBRARY={dependencies / 'lib/libxml2.so'}",
                    f"-DLIBUCONTEXT={dependencies / 'lib/libucontext.so'}",
                    f"-DLIBSEMAPHORE:FILEPATH={semaphore}",
                    f"-DANDROID_POSIX_SEMAPHORE_LIB:FILEPATH={semaphore}",
                    f"-DZLIB_INCLUDE_DIR={dependencies / 'include'}",
                    f"-DZLIB_LIBRARY={dependencies / 'lib/libz.so'}"]
    else:
        require((build / "CMakeCache.txt").is_file(), "configure the build first")
        command = ["cmake", "--build", str(build), "--parallel", "2"]
        if args.host_only:
            command += ["--target", "darling", "mldr", "darlingserver"]
    os.environ["PKG_CONFIG_LIBDIR"] = str(dependencies / "lib/pkgconfig")
    os.environ.pop("PKG_CONFIG_PATH", None)
    termux_suffix = "/data/data/com.termux/files/usr"
    if str(dependencies).endswith(termux_suffix):
        os.environ["PKG_CONFIG_SYSROOT_DIR"] = str(dependencies)[:-len(termux_suffix)]
    run(command, build / f"{args.phase}.log", args.timeout)
    if args.phase == "build":
        inspect_native_outputs(build)
    print(f"Darling Android {args.phase} completed; no runtime installed or executed")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        sys.exit(f"native runtime build: {error}")
