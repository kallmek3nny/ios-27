#ifndef IOS27_PHASE22A_SPACE_H
#define IOS27_PHASE22A_SPACE_H

#include <stddef.h>
#include <stdint.h>

#define IOS27_22A_PAGE 4096u
#define IOS27_22A_MAX_REGIONS 128u
#define IOS27_22A_MAX_PINS 128u
#define IOS27_22A_READ 1u
#define IOS27_22A_WRITE 2u
#define IOS27_22A_EXEC 4u
#define IOS27_22A_GUARD_BEFORE 1u
#define IOS27_22A_GUARD_AFTER 2u

typedef enum {
    IOS27_22A_OK = 0,
    IOS27_22A_INVALID = -1,
    IOS27_22A_CLOSED = -2,
    IOS27_22A_PERM = -3,
    IOS27_22A_LIMIT = -4,
    IOS27_22A_BUSY = -5,
    IOS27_22A_STALE = -6,
    IOS27_22A_FAULT = -7
} ios27_22a_result;

typedef struct ios27_22a_space ios27_22a_space;

typedef struct {
    uint64_t address_limit;
    uint64_t committed_limit;
    uint32_t region_limit;
} ios27_22a_config;

typedef struct {
    void *host;
    size_t length;
    uint64_t token;
} ios27_22a_view;

typedef struct {
    uint64_t committed;
    uint32_t regions;
    uint32_t pins;
    uint32_t closed;
} ios27_22a_stats;

/* Synthetic 64-bit guest offsets, NOT host addresses or Darwin process isolation.
 * Page zero is permanently inaccessible. No API executes guest code.
 * All public operations except destroy are mutex-serialized. Callers must
 * join external threads before destroy; close may run concurrently.
 */
int ios27_22a_create(const ios27_22a_config *config, ios27_22a_space **out);
int ios27_22a_map(ios27_22a_space *space, uint64_t address, uint64_t bytes,
                  unsigned permissions, unsigned guards, uint64_t *handle);
/* A non-executable stack with both hardware-backed guard pages. */
int ios27_22a_map_stack(ios27_22a_space *space, uint64_t address,
                        uint64_t bytes, uint64_t *handle);
int ios27_22a_protect(ios27_22a_space *space, uint64_t handle,
                      unsigned permissions);
int ios27_22a_unmap(ios27_22a_space *space, uint64_t handle);
int ios27_22a_probe(ios27_22a_space *space, uint64_t address,
                    uint64_t bytes, uint64_t alignment, unsigned permissions);
int ios27_22a_copyin(ios27_22a_space *space, void *destination,
                     uint64_t address, size_t bytes);
int ios27_22a_copyout(ios27_22a_space *space, uint64_t address,
                      const void *source, size_t bytes);
/* Borrow a contiguous range within ONE mapping. A pin blocks unmap/close.
 * Never dereference host after release, or concurrently with mutation.
 * Each pin has its own single-use generation-checked release token.
 */
int ios27_22a_pin(ios27_22a_space *space, uint64_t address,
                  size_t bytes, uint64_t alignment, unsigned permissions,
                  ios27_22a_view *view);
int ios27_22a_unpin(ios27_22a_space *space, ios27_22a_view *view);
int ios27_22a_snapshot(ios27_22a_space *space, ios27_22a_stats *stats);
/* Inject one failure after N successful qualifying mappings (N=0: next map).
 * Test-only and deterministic under serialized calls, not a security control.
 */
int ios27_22a_fail_map_after(ios27_22a_space *space, uint32_t count);
int ios27_22a_close(ios27_22a_space *space);
int ios27_22a_destroy(ios27_22a_space **space);
#endif
