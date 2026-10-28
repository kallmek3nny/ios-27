#ifndef IOS27_LIBSYSTEM21J_DIAGNOSTICS_H
#define IOS27_LIBSYSTEM21J_DIAGNOSTICS_H

#include <stddef.h>
#include <stdint.h>

/* Native diagnostic harness, NOT a Darwin pthread layout or guest ABI. */
#define IOS27_21J_MAX_LEASES 64u
#define IOS27_21J_TRACE_CAPACITY 1024u
typedef struct ios27_21j_diagnostics ios27_21j_diagnostics;
typedef struct { uint32_t slot, generation; uint64_t owner; } ios27_21j_lease;
enum ios27_21j_event_kind {
    IOS27_21J_CREATE = 1, IOS27_21J_RELEASE = 2,
    IOS27_21J_CONTENTION = 3, IOS27_21J_WAIT = 4,
    IOS27_21J_TIMEOUT = 5, IOS27_21J_CHECKPOINT = 6,
    IOS27_21J_INJECTION = 7
};
typedef struct {
    uint64_t sequence;
    uint64_t monotonic_ns;
    uint32_t actor;
    uint32_t kind;
    int32_t result;
    uint32_t detail;
} ios27_21j_event;
typedef struct {
    size_t active, peak, capacity;
    uint64_t next_sequence, dropped, injections, probes;
    uint64_t contention, waits, timeouts, leases_created, leases_released;
} ios27_21j_snapshot_data;

/* Caller must join all users before destroy, even if active == 0. */
int ios27_21j_create(ios27_21j_diagnostics **out, size_t capacity,
                     uint64_t seed, unsigned failure_per_10000);
int ios27_21j_destroy(ios27_21j_diagnostics *diag);
/* Stable fault selection from (seed, stage, actor, ordinal), independent
 * of host scheduling. No real operation is faulted unless caller opts in. */
int ios27_21j_probe(ios27_21j_diagnostics *diag, uint32_t stage,
                    uint32_t actor, uint64_t ordinal);
int ios27_21j_acquire(ios27_21j_diagnostics *diag,
                       ios27_21j_lease *out);
int ios27_21j_release(ios27_21j_diagnostics *diag, ios27_21j_lease lease);
int ios27_21j_record(ios27_21j_diagnostics *diag, uint32_t kind,
                      uint32_t actor, int32_t result, uint32_t detail);
int ios27_21j_snapshot(ios27_21j_diagnostics *diag,
                        ios27_21j_snapshot_data *out);
/* ERANGE means an event was overwritten in the fixed-size ring. */
int ios27_21j_read(ios27_21j_diagnostics *diag, uint64_t sequence,
                    ios27_21j_event *out);
#endif
