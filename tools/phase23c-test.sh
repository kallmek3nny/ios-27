#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
work="${1:-$repo/.phase23c-build}"
mkdir -p "$work"
: "${CC:=clang}"
: "${DARWIN_CLANG:=clang}"
for name in root other protocol category; do
    timeout 30s "$DARWIN_CLANG" --target=arm64-apple-darwin \
       -fobjc-runtime=macosx-11.0 -O2 -fno-objc-arc -fno-stack-protector \
       -Werror -Wall -x objective-c -c "$repo/tests/phase23c-$name.m" \
       -o "$work/phase23c-$name.o"
done
common=("$repo/src/objc_metadata.c" "$repo/src/phase22d_metadata.c"
        "$repo/src/phase22d_runtime.c" "$repo/src/phase23c_bridge.c")
flags=(-std=c11 -Wall -Wextra -Wpedantic -Werror -g -O1 -pthread
       -I "$repo/include")
if [[ "${PHASE23C_SANITIZE:-0}" == 1 ]]; then
    flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
for test in stress reject; do
    timeout 300s "$CC" "${flags[@]}" "${common[@]}" \
        "$repo/tests/phase23c-$test.c" -o "$work/phase23c-$test"
done
export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1"
export UBSAN_OPTIONS="halt_on_error=1"
timeout 180s "$work/phase23c-stress" "$work/phase23c-root.o" "$work/phase23c-other.o"
timeout 180s "$work/phase23c-reject" "$work/phase23c-protocol.o" "$work/phase23c-category.o"
