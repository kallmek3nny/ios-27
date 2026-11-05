#ifndef IOS27_PHASE25A_INSPECT_H
#define IOS27_PHASE25A_INSPECT_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t segments;
    uint32_t sections;
    uint32_t metadata_sections;
    uint32_t chained_fixup_bytes;
    uint32_t class_slots;
    uint32_t category_slots;
    uint32_t selector_slots;
} ios27_phase25a_summary;

/* Non-executing, bounded ARM64 MH_DYLIB preflight. Never trusts load commands,
 * file offsets or section extents. Does not accept arm64e or classic fixups.
 * This is NOT a replacement for relocation-aware phase25a_bind inspection. */
int ios27_phase25a_preflight(const uint8_t *file, size_t size,
                            ios27_phase25a_summary *out);
#endif
