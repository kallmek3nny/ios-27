#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21j_diagnostics.h"
#include "ios27/libsystem21j_lockorder.h"
#include "ios27/libsystem21j_schedule.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21j boundaries:%d %s\n", __LINE__, #x); abort(); \
} } while (0)
enum { CASES = 30000 };
static uint64_t scramble(uint64_t x) {
    x += UINT64_C(0x9e3779b97f4a7c15);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}
static void diagnostic_boundaries(void) {
    ios27_21j_diagnostics *d = NULL;
    ios27_21j_snapshot_data stats;
    ios27_21j_lease lease;
    ios27_21j_event entries[8] = {{0}};
    ios27_21j_event event;
    size_t needed = 0;
    uint64_t first = 0, last = 0;
    CHECK(ios27_21j_create(&d, 2, 2, 0) == 0);
    CHECK(ios27_21j_acquire(d, NULL) == EINVAL);
    CHECK(ios27_21j_acquire(d, &lease) == 0);
    CHECK(ios27_21j_destroy(d) == EBUSY);
    CHECK(ios27_21j_release(d, (ios27_21j_lease){0}) == EINVAL);
    CHECK(ios27_21j_release(d, (ios27_21j_lease){3, 1, lease.owner}) == EINVAL);
    CHECK(ios27_21j_release(d, (ios27_21j_lease){lease.slot, 0, lease.owner}) == EINVAL);
    CHECK(ios27_21j_release(d, (ios27_21j_lease){lease.slot, lease.generation, 0}) == EINVAL);
    for (uint64_t i = 0; i < CASES; ++i) {
        ios27_21j_lease fake = {
            (uint32_t)(scramble(i) % 256u),
            (uint32_t)scramble(i ^ UINT64_C(0xfedcab)),
            UINT64_MAX
        };
        CHECK(ios27_21j_release(d, fake) == EINVAL);
        CHECK(ios27_21j_probe(d, 0, (uint32_t)i, i) == EINVAL);
        CHECK(ios27_21j_record(d, 0, (uint32_t)i, 0, 0) == EINVAL);
        CHECK(ios27_21j_record(d, 8u + (uint32_t)(i % 100u),
                                (uint32_t)i, 0, 0) == EINVAL);
        CHECK(ios27_21j_read(d, 0, &event) == EINVAL);
        CHECK(ios27_21j_read(d, UINT64_MAX, &event) == ERANGE);
    }
    CHECK(ios27_21j_export(d, entries, 8, &needed, &first, &last) == 0);
    CHECK(needed == 1 && first == 1 && last == 1);
    CHECK(ios27_21j_export(d, entries, 0, &needed, &first, &last) == ENOSPC);
    CHECK(needed == 1 && entries[0].sequence == 1);
    CHECK(ios27_21j_note_contention(NULL, 0, 4) == EINVAL);
    CHECK(ios27_21j_snapshot(NULL, &stats) == EINVAL);
    CHECK(ios27_21j_release(d, lease) == 0);
    CHECK(ios27_21j_release(d, lease) == EINVAL);
    CHECK(ios27_21j_snapshot(d, &stats) == 0);
    CHECK(stats.active == 0 && stats.leases_created == stats.leases_released);
    CHECK(stats.probes == 0 && stats.injections == 0);
    CHECK(ios27_21j_destroy(d) == 0);
}
static void schedule_boundaries(void) {
    const uint32_t plan[] = {0, 1};
    const uint32_t invalid[] = {0, 2};
    ios27_21j_schedule *s = NULL;
    size_t done = 0;
    CHECK(ios27_21j_schedule_create(NULL, 2, plan, 2) == EINVAL);
    CHECK(ios27_21j_schedule_create(&s, 2, NULL, 2) == EINVAL);
    CHECK(ios27_21j_schedule_create(&s, 2, invalid, 2) == EINVAL);
    CHECK(ios27_21j_schedule_create(&s, IOS27_21J_MAX_ACTORS + 1u,
                                   plan, 2) == EINVAL);
    CHECK(ios27_21j_schedule_create(&s, 2, plan,
                                     IOS27_21J_MAX_TURNS + 1u) == EINVAL);
    CHECK(ios27_21j_schedule_create(&s, 2, plan, 2) == 0);
    CHECK(ios27_21j_turn_acquire(s, 0, 0, 60001) == EINVAL);
    CHECK(ios27_21j_turn_acquire(s, 0, 1, 5000) == EINVAL);
    CHECK(ios27_21j_turn_acquire(s, 2, 0, 5000) == EINVAL);
    CHECK(ios27_21j_turn_acquire(s, 0, 2, 5000) == EINVAL);
    CHECK(ios27_21j_turn_release(s, 0, 0) == EPERM);
    CHECK(ios27_21j_turn_acquire(s, 0, 0, 5000) == 0);
    CHECK(ios27_21j_schedule_destroy(s) == EBUSY);
    CHECK(ios27_21j_turn_acquire(s, 0, 0, 5000) == EDEADLK);
    CHECK(ios27_21j_turn_release(s, 1, 0) == EPERM);
    CHECK(ios27_21j_turn_release(s, 0, 0) == 0);
    CHECK(ios27_21j_turn_acquire(s, 0, 0, 0) == EALREADY);
    CHECK(ios27_21j_turn_acquire(s, 1, 1, 5000) == 0);
    CHECK(ios27_21j_turn_release(s, 1, 1) == 0);
    CHECK(ios27_21j_schedule_snapshot(s, &done, NULL, NULL, NULL) == 0);
    CHECK(done == 2);
    CHECK(ios27_21j_schedule_destroy(s) == 0);
}
static void lock_graph_boundaries(void) {
    ios27_21j_lockorder *g = NULL;
    ios27_21j_lockorder_stats stats;
    CHECK(ios27_21j_lockorder_create(&g, 2, 2) == 0);
    CHECK(ios27_21j_lockorder_wait(g, 0, 0) == EAGAIN);
    CHECK(ios27_21j_lockorder_acquire(g, 0, 0) == 0);
    CHECK(ios27_21j_lockorder_acquire(g, 2, 0) == EINVAL);
    CHECK(ios27_21j_lockorder_wait(g, 3, 0) == EINVAL);
    CHECK(ios27_21j_lockorder_wait(g, 0, 2) == EINVAL);
    CHECK(ios27_21j_lockorder_wait(g, 1, 0) == 0);
    CHECK(ios27_21j_lockorder_wait(g, 1, 0) == EALREADY);
    CHECK(ios27_21j_lockorder_acquire(g, 1, 1) == EBUSY);
    CHECK(ios27_21j_lockorder_cancel_wait(g, 1, 1) == EINVAL);
    CHECK(ios27_21j_lockorder_reset(g) == EBUSY);
    CHECK(ios27_21j_lockorder_destroy(g) == EBUSY);
    CHECK(ios27_21j_lockorder_cancel_wait(g, 1, 0) == 0);
    CHECK(ios27_21j_lockorder_release(g, 0, 0) == 0);
    CHECK(ios27_21j_lockorder_snapshot(g, &stats) == 0);
    CHECK(stats.active == 0 && stats.waiting == 0);
    CHECK(ios27_21j_lockorder_reset(g) == 0);
    CHECK(ios27_21j_lockorder_destroy(g) == 0);
}
int main(void) {
    diagnostic_boundaries();
    schedule_boundaries();
    lock_graph_boundaries();
    printf("phase21j boundaries: %u malformed scalar scenarios, no partial object mutation; PASS\n",
           CASES * 6u);
    return 0;
}
