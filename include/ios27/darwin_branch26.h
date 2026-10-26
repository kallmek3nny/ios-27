#ifndef IOS27_DARWIN_BRANCH26_H
#define IOS27_DARWIN_BRANCH26_H

#include <stddef.h>
#include <stdint.h>

/*
 * Phase 13: a deliberately narrow relocation plan for real Clang-produced
 * arm64-apple-darwin MH_OBJECT files with exactly one external BRANCH26
 * relocation to the public-source _fixture_entry symbol.
 *
 * The parser never executes guest data or binds arbitrary external names.
 * This is a test-only native ARM64 call across separately compiled objects,
 * NOT a Darwin executable loader or dyld implementation.
 */
typedef struct {
    uint64_t text_file_offset;
    uint64_t text_size;
    uint64_t entry_offset;
    uint64_t branch_offset;
} ios27_darwin_branch26_plan;

int ios27_darwin_branch26_inspect(const uint8_t *file, size_t size,
                                  ios27_darwin_branch26_plan *out,
                                  char *error, size_t error_cap);

/* Patch an independent caller-owned writable copy, never input Mach-O.
 * loaded_address and destination_address are caller-approved code addresses.
 * Any failure leaves the output text completely unmodified.
 * Only AArch64 BL with zero displacement is recognized.
 */
int ios27_darwin_branch26_apply(uint8_t *text, size_t text_size,
                                const ios27_darwin_branch26_plan *plan,
                                uintptr_t loaded_address,
                                uintptr_t destination_address,
                                char *error, size_t error_cap);

#endif
