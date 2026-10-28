#ifndef IOS27_LIBSYSTEM21J_LOCKORDER_H
#define IOS27_LIBSYSTEM21J_LOCKORDER_H
#include <stddef.h>
#include <stdint.h>

/* Opt-in native logical lock-order diagnostics: not a Darwin ABI bridge,
   not a substitute for observing actual application lock transitions. */
#define IOS27_21J_GRAPH_MAX_LOCKS 64u
#define IOS27_21J_GRAPH_MAX_ACTORS 64u
typedef struct ios27_21j_lockorder ios27_21j_lockorder;
typedef struct {
    size_t locks, actors, active, peak, edges, waiting;
    uint64_t acquisitions, releases, contention, cycles_rejected, wait_cycles_rejected, resets;
} ios27_21j_lockorder_stats;
int ios27_21j_lockorder_create(ios27_21j_lockorder **out,
                               size_t locks, size_t actors);
/* Caller must join all participating actors before destroy. */
int ios27_21j_lockorder_destroy(ios27_21j_lockorder *graph);
/* Claim an instrumented logical lock. EBUSY for another owner, EDEADLK
   for recursive acquisition or a prospective lock-order dependency cycle. */
int ios27_21j_lockorder_acquire(ios27_21j_lockorder *graph,
                                uint32_t actor, uint32_t lock_id);
/* EPERM on foreign or double release. Locks need not be released LIFO. */
int ios27_21j_lockorder_release(ios27_21j_lockorder *graph,
                                uint32_t actor, uint32_t lock_id);
/* Register a blocked acquisition while another actor owns lock_id.
   EBUSY for unowned lock, EALREADY for duplicate pending registration,
   EDEADLK if the prospective wait-for dependency closes a cycle.
   Returns zero only when registration succeeded and caller may block. */
int ios27_21j_lockorder_wait(ios27_21j_lockorder *graph,
                             uint32_t actor, uint32_t lock_id);
/* Remove an outstanding wait before recording successful lock acquisition. */
int ios27_21j_lockorder_cancel_wait(ios27_21j_lockorder *graph,
                                    uint32_t actor, uint32_t lock_id);
/* Clear observed dependency edges only when no logical lock is held. */
int ios27_21j_lockorder_reset(ios27_21j_lockorder *graph);
int ios27_21j_lockorder_snapshot(ios27_21j_lockorder *graph,
                                 ios27_21j_lockorder_stats *out);
/* Returns 1 or 0 through out; only direct previously observed edges. */
int ios27_21j_lockorder_edge(ios27_21j_lockorder *graph,
                             uint32_t from, uint32_t to, int *out);
#endif
