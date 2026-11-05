#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${CC:=cc}"
flags=(-std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror -Iinclude)
if [[ "${PHASE27E_SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
"$CC" "${flags[@]}" src/phase27e_demo.c tests/phase27e-demo-test.c -lm -o "$out/demo"
timeout 180s "$out/demo"