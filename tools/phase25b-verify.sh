#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp="$(timeout 5s mktemp -d)"
trap 'timeout 5s rm -rf "$tmp"' EXIT
cc="${CC:-clang}"
flags=(-std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror -pthread -I "$root/include")
if [[ "${PHASE25B_SANITIZE:-0}" == 1 ]]; then
    flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
# Host verification: metadata-only; no Darwin instructions are executed on x86.
timeout 120s "$cc" "${flags[@]}" "$root/src/phase25b_dispatch.c" \
    "$root/tests/phase25b-host.c" -o "$tmp/phase25b-host"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
    timeout 180s "$tmp/phase25b-host"
timeout 30s clang --target=arm64-apple-darwin \
    -c "$root/src/phase25b_entry.S" -o "$tmp/phase25b-darwin-entry.o"
timeout 10s file "$tmp/phase25b-darwin-entry.o" | timeout 10s grep -q 'Mach-O 64-bit arm64 object'
# Cross-check AArch64 Linux entrypoint machine-code compilation, even on x86.
timeout 30s clang --target=aarch64-linux-gnu \
    -c "$root/src/phase25b_entry.S" -o "$tmp/phase25b-linux-entry.o"
if [[ "$(timeout 5s uname -m)" == aarch64 ]]; then
    timeout 120s "$cc" "${flags[@]}" \
        "$root/src/phase25b_dispatch.c" "$root/src/phase25b_entry.S" \
        "$root/tests/phase25b-native.c" -o "$tmp/phase25b-native"
    PHASE25B_NATIVE_BIN="$tmp/phase25b-native" \
        timeout 180s python3 "$root/tools/phase25b-link.py"
else
    timeout 180s python3 "$root/tools/phase25b-link.py"
    echo 'phase25b: x86_64 only metadata and linked Mach-O inspection; native send execution requires arm64'
fi
