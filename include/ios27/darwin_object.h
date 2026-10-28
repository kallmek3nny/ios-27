#ifndef IOS27_DARWIN_OBJECT_H
#define IOS27_DARWIN_OBJECT_H

#include <stddef.h>
#include <stdint.h>

/*
 * Bounded inspection of an actual clang-produced arm64-apple-darwin
 * MH_OBJECT file, not a linked executable, shared cache, or Apple binary.
 * Only freestanding relocation-free __TEXT/__text fixture objects are
 * supported. Parsing does not map or execute native memory.
 */
typedef struct {
    uint32_t platform;
    uint32_t minos;
    uint64_t text_file_offset;
    uint64_t text_size;
    uint64_t entry_offset; /* offset within the validated __text section */
    uint32_t symbol_count;
} ios27_darwin_object;

int ios27_darwin_object_inspect(const uint8_t *file, size_t file_size,
                                ios27_darwin_object *out,
                                char *error, size_t error_capacity);
/*
 * Phase 15a: inspect, but never map or execute, a genuine SDK-free
 * LLVM-linked ARM64 Darwin MH_EXECUTE file.
 */
typedef struct {
    uint64_t entry_file_offset;
    uint64_t text_file_offset;
    uint64_t text_size;
    uint32_t platform;
    uint32_t minos;
    uint32_t segment_count;
    uint32_t symbol_count;
} ios27_darwin_executable;

int ios27_darwin_executable_inspect(const uint8_t *file, size_t size,
                                    ios27_darwin_executable *out,
                                    char *error, size_t error_cap);
#endif
