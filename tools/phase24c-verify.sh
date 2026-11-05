#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cc="${CC:-clang}"
clang="${DARWIN_CLANG:-clang}"
flags=(-std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror -pthread -I "$root/include")
if [[ "${PHASE24C_SANITIZE:-0}" == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
timeout 120s "$cc" "${flags[@]}" "$root/src/phase24c_registry.c" \
  "$root/tests/phase24c-registry.c" -o "$work/registry"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  timeout 180s "$work/registry"
for name in libalpha libbeta libgamma libdelta libepsilon libzeta appone apptwo appthree; do
  timeout 30s "$clang" --target=arm64-apple-darwin \
    -fobjc-runtime=macosx-11.0 -fno-objc-arc -fno-stack-protector \
    -Werror -Wall -Wextra -O2 -c "$root/tests/phase24c-$name.m" \
    -o "$work/$name.o"
  timeout 30s python3 - "$work/$name.o" <<'PY'
import pathlib, struct, sys
p = pathlib.Path(sys.argv[1]).read_bytes()
assert len(p) > 64
magic, cpu, subtype, filetype, count, size, flags, reserved = struct.unpack_from('<8I', p)
assert (magic, cpu, filetype) == (0xfeedfacf, 0x0100000c, 1)
assert count and size <= len(p)-32 and reserved == 0
pos = 32
for i in range(count):
    kind, length = struct.unpack_from('<II', p, pos)
    assert length >= 8 and not length % 8 and pos + length <= 32 + size
    pos += length
assert pos == 32 + size
assert b'__objc_' in p and b'_objc_msgSend' in p or b'__objc_' in p
PY
done
printf 'phase24c: compiled 9 compiler-produced arm64 Darwin MH_OBJECT fixtures\n'
timeout 240s python3 "$root/tools/phase24c-link.py"
