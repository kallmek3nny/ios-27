#ifndef IOS27_LIBSYSTEM21J_SCHEDULE_H
#define IOS27_LIBSYSTEM21J_SCHEDULE_H

#include <stddef.h>
#include <stdint.h>

/* Host-only deterministic turn scheduler for reproducing interleavings.
   External operations are ordered ONLY while the caller holds its turn.
   Not a Darwin mutex/condition variable ABI implementation. */
#define IOS27_21J_MAX_TURNS 4096u
#define IOS27_21J_MAX_ACTORS 128u
typedef struct ios27_21j_schedule ios27_21j_schedule;
int ios27_21j_schedule_create(ios27_21j_schedule **out,
                               uint32_t actors, const uint32_t *plan,
                               size_t steps);
/* EBUSY if an actor holds a turn or callers are waiting. */
int ios27_21j_schedule_destroy(ios27_21j_schedule *schedule);
/* Wait at most timeout_ms on CLOCK_MONOTONIC for a turn.
   EALREADY rejects an already completed turn; ETIMEDOUT permits retry.
   This intentionally does not implement native asynchronous cancellation. */
int ios27_21j_turn_acquire(ios27_21j_schedule *schedule, uint32_t actor,
                            size_t step, uint64_t timeout_ms);
int ios27_21j_turn_release(ios27_21j_schedule *schedule, uint32_t actor,
                            size_t step);
int ios27_21j_schedule_snapshot(ios27_21j_schedule *schedule,
                                 size_t *completed, size_t *total,
                                 size_t *waiting, int *held);
#endif
