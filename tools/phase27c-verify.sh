#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output="${1:-${TMPDIR:-/tmp}/phase27c-software-render}"
compiler="${CC:-clang}"
flags=(-std=c11 -Wall -Wextra -Werror -Wpedantic -pthread -O1 -g)
if [[ "${P27C_SANITIZE:-0}" == "1" ]]; then
    flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
timeout 120s "$compiler" "${flags[@]}" -I"$root/include" \
    "$root/src/phase26c_graphics.c" "$root/src/phase27c_renderer.c" \
    "$root/tests/phase27c-software.c" -o "$output"
timeout 180s "$output"
