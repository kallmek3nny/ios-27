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


/* Phase 19d: bounded, transactional relocation of independently staged
 * ARM64 Mach-O sections. Symbol addresses and GOT slots must already have
 * been resolved and approved by the caller. No PAC forgery/signing. */
typedef struct {
    uint32_t offset;
    uint8_t type;        /* Mach-O ARM64_RELOC_* */
    uint8_t length;      /* 2 = instruction/32-bit; 3 = 64-bit pointer */
    uint8_t pcrel;
    uint8_t external;
    uintptr_t target;    /* resolved address; GOT slot for GOT relocations */
    int64_t addend;
} ios27_darwin_arm64_reloc;

/* All-or-nothing: any invalid relocation leaves the entire section intact.
 * Supports UNSIGNED, SUBTRACTOR+UNSIGNED, BRANCH26, PAGE21, PAGEOFF12,
 * GOT_LOAD_PAGE21, GOT_LOAD_PAGEOFF12, POINTER_TO_GOT and ADDEND pairs.
 * Authenticated-pointer/PAC relocations are rejected explicitly. */
int ios27_darwin_arm64_apply_batch(uint8_t *section, size_t section_size,
                                  uintptr_t loaded_address,
                                  const ios27_darwin_arm64_reloc *relocs,
                                  size_t count, char *error, size_t error_cap);


/* Phase 19d section linker: genuine compiler-produced SDK-free MH_OBJECT
 * inputs; resolves external and local symbols and stages mixed code, const,
 * writable data and BSS. Regions are page-aligned for RX/R/RW sealing.
 * Caller controls the approved object bytes and mapping. Not a sandbox,
 * dyld implementation, or an Apple SDK replacement. */
typedef struct {
    const uint8_t *bytes;
    size_t size;
} ios27_darwin_link_input;
typedef struct {
    size_t image_bytes;
    size_t rx_bytes;
    size_t ro_bytes;
    size_t rw_bytes;
    size_t entry_offset;
} ios27_darwin_link_layout;

/* Atomic output: bytes and layout unchanged/zeroed respectively on error.
 * Expects output to be separate, writable staging memory; caller seals
 * mapped RX/R/RW page regions and clears icache before native execution.
 * Only 4K and 16K page-granule layouts are supported. */
int ios27_darwin_link_objects(const ios27_darwin_link_input *objects,
                              size_t object_count, const char *entry_name,
                              uint8_t *output, size_t output_cap,
                              uintptr_t load_address, size_t page_size,
                              ios27_darwin_link_layout *layout,
                              char *error, size_t error_cap);

#endif
