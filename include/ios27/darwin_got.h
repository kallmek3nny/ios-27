#ifndef IOS27_DARWIN_GOT_H
#define IOS27_DARWIN_GOT_H

#include <stddef.h>
#include <stdint.h>

/* Narrow, fail-closed plans for TWO genuine, SDK-free Clang MH_OBJECT files.
 * This is not dyld, a general-purpose linker or a Mach-O executable loader.
 * Only a known _fixture_read_data -> _fixture_data GOT reference is accepted.
 */
typedef struct {
    uint64_t text_file_offset;
    uint64_t text_size;
    uint64_t entry_offset;
    uint32_t adrp_offset;
    uint32_t ldr_offset;
} ios27_darwin_got_plan;

typedef struct {
    uint64_t data_file_offset;
    uint64_t data_size;
} ios27_darwin_const_plan;

int ios27_darwin_got_inspect(const uint8_t *file, size_t file_size,
                            ios27_darwin_got_plan *out,
                            char *error, size_t error_cap);
int ios27_darwin_const_inspect(const uint8_t *file, size_t file_size,
                              ios27_darwin_const_plan *out,
                              char *error, size_t error_cap);

/* Patch ONLY caller-owned writable text after complete validation.
 * pc_address and got_address are in the same native process; GOT must
 * contain the caller-approved data pointer. No rollback is ever partial.
 * Caller seals text RX, GOT and const data R before execution.
 */
int ios27_darwin_got_apply(uint8_t *text, size_t size,
                           const ios27_darwin_got_plan *plan,
                           uintptr_t pc_address, uintptr_t got_address,
                           char *error, size_t error_cap);
#endif
