#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
build="${IOS27_PHASE22C_BUILD:-build/phase22c}"
cc="${CC:-clang}"
darwin_clang="${DARWIN_CLANG:-clang}"
flags=(-std=c11 -Wall -Wextra -Werror -Wpedantic -O1 -g -Iinclude)
cmake_flags=()
if [[ "${IOS27_PHASE22C_SANITIZE:-0}" == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
  cmake_flags+=("-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -g")
  export ASAN_OPTIONS="halt_on_error=1:detect_leaks=1"
  export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"
fi
printf 'phase22c start=%s compiler=%s sanitizer=%s\n' "$(date -u +%FT%TZ)" "$cc" "${IOS27_PHASE22C_SANITIZE:-0}"
timeout 120s cmake -S . -B "$build" -DIOS27_BUILD_TESTS=ON "${cmake_flags[@]}"
timeout 300s cmake --build "$build" --parallel 2
timeout 120s "$cc" "${flags[@]}" src/phase22c_graph.c tests/phase22c-graph.c "$build/libios27_core.a" -o "$build/phase22c-graph"
timeout 120s "$cc" "${flags[@]}" src/phase22c_graph.c tests/phase22c-compiled.c "$build/libios27_core.a" -o "$build/phase22c-compiled"
timeout 180s "$build/phase22c-graph"
timeout 30s "$darwin_clang" --target=arm64-apple-darwin -ffreestanding -fno-builtin -fno-stack-protector -O2 -c tests/phase22c-fixture-provider.c -o "$build/phase22c-provider.o"
timeout 30s "$darwin_clang" --target=arm64-apple-darwin -ffreestanding -fno-builtin -fno-stack-protector -O2 -c tests/phase22c-fixture-consumer.c -o "$build/phase22c-consumer.o"
timeout 180s "$build/phase22c-compiled" "$build/phase22c-provider.o" "$build/phase22c-consumer.o"
# Some installed ld64.lld variants are stubs without a Mach-O backend.
linker="${DARWIN_LINKER:-ld64.lld}"
# Never claim linked dylib or native guest execution coverage in that case.
if command -v "$linker" >/dev/null && timeout 30s "$linker" -arch arm64 -platform_version macos 11.0 11.0 -dylib -install_name provider.dylib -o "$build/phase22c-provider.dylib" "$build/phase22c-provider.o"; then
  timeout 30s "$linker" -arch arm64 -platform_version macos 11.0 11.0 -dylib -install_name consumer.dylib "$build/phase22c-provider.dylib" -o "$build/phase22c-consumer.dylib" "$build/phase22c-consumer.o"
  timeout 180s "$build/phase22c-compiled" "$build/phase22c-provider.o" "$build/phase22c-consumer.o" "$build/phase22c-provider.dylib" "$build/phase22c-consumer.dylib"
else
  printf '%s\n' 'phase22c NOT VERIFIED: compiler-linked MH_DYLIB tests unavailable (ld64.lld missing or unsupported)' >&2
fi
printf 'phase22c end=%s\n' "$(date -u +%FT%TZ)"
