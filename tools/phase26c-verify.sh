#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${1:-${TMPDIR:-/tmp}/phase26c-headless-test}"
cc="${CC:-clang}"
flags=(-std=c11 -Wall -Wextra -Werror -Wpedantic -pthread -O1 -g)
if [[ "${P26_SANITIZE:-0}" == "1" ]]; then
    flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
timeout 120s "$cc" "${flags[@]}" -I"$root/include" \
    "$root/src/phase26c_graphics.c" "$root/android/phase26c/ahb_bridge.c" \
    "$root/tests/phase26c-headless.c" -o "$out"
timeout 180s "$out"