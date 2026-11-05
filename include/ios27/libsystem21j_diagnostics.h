#ifndef IOS27_LIBSYSTEM21J_DIAGNOSTICS_H
#define IOS27_LIBSYSTEM21J_DIAGNOSTICS_H

#include <stddef.h>
#include <stdint.h>

/* Native diagnostic harness, NOT a Darwin pthread layout or guest ABI. */
#define IOS27_21J_MAX_LEASES 64u
#define IOS27_21J_TRACE_CAPACITY 1024u
#define IOS27_21J_MAX_FAULT_RULES 16u
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
    uint64_t contention_ns_total, contention_ns_max;
    /* [0,1us), [1,10us), [10,100us), [100us,1ms), [1ms,+inf). */
    uint64_t contention_ns_buckets[5];
} ios27_21j_snapshot_data;

/* Caller must join all users before destroy, even if active == 0. */
int ios27_21j_create(ios27_21j_diagnostics **out, size_t capacity,
                     uint64_t seed, unsigned failure_per_10000);
int ios27_21j_destroy(ios27_21j_diagnostics *diag);
/* Stable fault selection from (seed, stage, actor, ordinal), independent
 * of host scheduling. No real operation is faulted unless caller opts in. */
int ios27_21j_probe(ios27_21j_diagnostics *diag, uint32_t stage,
                    uint32_t actor, uint64_t ordinal);
/* One-shot configuration of an exact deterministic probe override.
   Rules can only be changed before the first probe, to make concurrent
   scheduling irrelevant to the selected fault outcomes. inject must be 0/1.
   EEXIST for duplicate keys; ENOSPC for the bounded rule table. */
int ios27_21j_rule_add(ios27_21j_diagnostics *diag, uint32_t stage,
                       uint32_t actor, uint64_t ordinal, int inject);
int ios27_21j_rules_clear(ios27_21j_diagnostics *diag);

int ios27_21j_acquire(ios27_21j_diagnostics *diag,
                       ios27_21j_lease *out);
/* Fault-injected acquisition leaves out invalid and consumes no lease on ENOMEM. */
int ios27_21j_acquire_injected(ios27_21j_diagnostics *diag,
                              uint32_t stage, uint32_t actor,
                              uint64_t ordinal, ios27_21j_lease *out);
int ios27_21j_release(ios27_21j_diagnostics *diag, ios27_21j_lease lease);
int ios27_21j_record(ios27_21j_diagnostics *diag, uint32_t kind,
                      uint32_t actor, int32_t result, uint32_t detail);
/* Records a measured contended wait, with saturating duration accumulation. */
int ios27_21j_note_contention(ios27_21j_diagnostics *diag,
                              uint32_t actor, uint64_t duration_ns);
int ios27_21j_snapshot(ios27_21j_diagnostics *diag,
                        ios27_21j_snapshot_data *out);
/* ERANGE means an event was overwritten in the fixed-size ring. */
int ios27_21j_read(ios27_21j_diagnostics *diag, uint64_t sequence,
                    ios27_21j_event *out);
/* Internal consistency check made atomically under the same guard as
   mutating operations. The report is valid even when the runtime is busy.
   EPROTO indicates an invariant violation and provides violation bits. */
#define IOS27_21J_AUDIT_LEASE_COUNTS 1u
#define IOS27_21J_AUDIT_SLOTS 2u
#define IOS27_21J_AUDIT_TRACE 4u
#define IOS27_21J_AUDIT_CONTENTION 8u
typedef struct {
    size_t occupied, retired, resident;
    uint64_t first, last;
    unsigned violations;
} ios27_21j_audit_data;
int ios27_21j_audit(ios27_21j_diagnostics *diag,
                    ios27_21j_audit_data *out);
/* Copy an ordered, internally coherent journal snapshot under one lock.
   ENOSPC returns the needed count without a partial copy. A zero-event
   snapshot reports first/last as zero. */
int ios27_21j_export(ios27_21j_diagnostics *diag,
                      ios27_21j_event *out, size_t capacity,
                      size_t *count, uint64_t *first, uint64_t *last);

#endif
