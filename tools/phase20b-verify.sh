#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cc="${CC:-cc}"
clang="${CLANG:-clang}"
tmp="$(timeout 5s mktemp -d)"
trap 'timeout 5s rm -rf "$tmp"' EXIT
# Independent -Werror host runtime regression; no shared build edits.
timeout 120s "$cc" -std=c11 -Wall -Wextra -Werror -Wpedantic -O2 \
    -I"$root/include" "$root/src/objc20b_dispatch.c" \
    "$root/tests/phase20b-dispatch.c" -o "$tmp/phase20b-host"
timeout 180s "$tmp/phase20b-host"
# Real Clang ObjC2 Mach-O metadata and executable call-site relocations.
for level in 0 2; do
    timeout 30s "$clang" --target=arm64-apple-darwin \
        -fobjc-runtime=macosx-10.15 -fno-objc-arc -ffreestanding \
        -fno-stack-protector -Wall -Wextra -Werror "-O$level" \
        -c "$root/tests/phase20b-clang.m" -o "$tmp/objc-$level.o"
    timeout 10s file "$tmp/objc-$level.o" | timeout 10s grep -q 'Mach-O 64-bit arm64 object'
    timeout 10s strings "$tmp/objc-$level.o" | timeout 10s grep -q '_objc_msgSendSuper2'
    timeout 10s strings "$tmp/objc-$level.o" | timeout 10s grep -q '_OBJC_CLASS_\$_P20Child'
done
# Real machine-code AArch64 Darwin entrypoints, not host stubs.
timeout 30s "$clang" --target=arm64-apple-darwin \
    -c "$root/src/objc20b_entry.S" -o "$tmp/entry.o"
timeout 10s file "$tmp/entry.o" | timeout 10s grep -q 'Mach-O 64-bit arm64 object'
# Check source itself with Clang in addition to the chosen host C compiler.
timeout 120s "$clang" -std=c11 -Wall -Wextra -Werror -Wpedantic -O2 \
    -I"$root/include" -c "$root/src/objc20b_dispatch.c" -o "$tmp/dispatch.o"
echo 'phase20b: native host tests + O0/O2 Darwin Clang ObjC2 + AArch64 entrypoints pass'
