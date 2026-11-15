#ifndef IOS27_INITIALIZERS_H
#define IOS27_INITIALIZERS_H

#include <stddef.h>
#include <stdint.h>

#define IOS27_INITIALIZERS_MAX 256u
#define IOS27_INIT_LEGACY_POINTERS 1u
#define IOS27_INIT_TERMINATORS 2u
#define IOS27_INIT_TLS 4u
#define IOS27_INIT_OBJC 8u
#define IOS27_INIT_SWIFT 16u

/* Read-only plan for modern S_INIT_FUNC_OFFSETS sections in linked generic
 * ARM64 MH_EXECUTE/MH_DYLIB files. These are uint32 offsets from __TEXT's
 * preferred VM address, not file offsets or already-relocated pointers.
 * Every initializer and LC_MAIN target must be in a file-backed instruction
 * section with RX protection. No pointer is called by this API.
 *
 * Unsupported startup requirements are reported as a bitset. The contained
 * C graph runner refuses every nonzero bit; an offset plan alone does not
 * initialize ObjC, Swift, TLS, legacy pointer tables or termination hooks.
 * On any error the complete output is zeroed and the diagnostic, if supplied,
 * is NUL-terminated. Input bytes are never modified. */
typedef struct {
    uint64_t addresses[IOS27_INITIALIZERS_MAX];
    size_t count;
    uint64_t entry_vmaddr; /* zero for MH_DYLIB */
    uint64_t stack_size;
    unsigned unsupported;
} ios27_initializer_plan;

int ios27_initializers_inspect(const uint8_t *data, size_t size,
                               ios27_initializer_plan *out,
                               char *error, size_t error_size);

#endif
