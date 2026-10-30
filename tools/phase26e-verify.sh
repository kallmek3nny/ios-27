#!/usr/bin/env bash
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
out="${TMPDIR:-/tmp}/phase26e-verify-$$"
trap 'rm -rf -- "$out"' EXIT
mkdir -p "$out"
cc="${CC:-clang}"
flags=(-std=c11 -Wall -Wextra -Wpedantic -Wconversion -Werror -g -O1 -pthread -I"$here/include")
timeout 120s "$cc" "${flags[@]}" "$here/src/phase26e_host.c" "$here/tests/phase26e-host-test.c" -lm -o "$out/host-test"
timeout 180s "$out/host-test"
if [[ "${PHASE26E_SANITIZE:-0}" == "1" ]]; then
  timeout 120s "$cc" "${flags[@]}" -fno-omit-frame-pointer -fsanitize=address,undefined \
    "$here/src/phase26e_host.c" "$here/tests/phase26e-host-test.c" -lm -o "$out/san-test"
  ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180s "$out/san-test"
fi