#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21j_lockorder.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21j lockorder:%d %s\n", __LINE__, #x); abort(); \
} } while (0)
enum { ACTORS = 8, ROUNDS = 3000, HELD_PER_ROUND = 4, LOCKS = 16 };
static uint64_t scramble(uint64_t x) {
    x += UINT64_C(0x9e3779b97f4a7c15);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}
static void errors_and_cycle(void) {
    ios27_21j_lockorder *g = NULL;
    ios27_21j_lockorder_stats stats;
    int edge = -1;
    CHECK(ios27_21j_lockorder_create(NULL, 3, 2) == EINVAL);
    CHECK(ios27_21j_lockorder_create(&g, 0, 2) == EINVAL);
    CHECK(ios27_21j_lockorder_create(&g, 3, 0) == EINVAL);
    CHECK(ios27_21j_lockorder_create(&g, 65, 2) == EINVAL);
    CHECK(ios27_21j_lockorder_create(&g, 3, 2) == 0);
    CHECK(ios27_21j_lockorder_acquire(g, 2, 0) == EINVAL);
    CHECK(ios27_21j_lockorder_acquire(g, 0, 3) == EINVAL);
    CHECK(ios27_21j_lockorder_acquire(g, 0, 0) == 0);
    CHECK(ios27_21j_lockorder_acquire(g, 0, 0) == EDEADLK);
    CHECK(ios27_21j_lockorder_acquire(g, 1, 0) == EBUSY);
    CHECK(ios27_21j_lockorder_release(g, 1, 0) == EPERM);
    CHECK(ios27_21j_lockorder_reset(g) == EBUSY);
    CHECK(ios27_21j_lockorder_destroy(g) == EBUSY);
    CHECK(ios27_21j_lockorder_acquire(g, 0, 1) == 0);
    CHECK(ios27_21j_lockorder_edge(g, 0, 1, &edge) == 0 && edge);
    CHECK(ios27_21j_lockorder_release(g, 0, 1) == 0);
    CHECK(ios27_21j_lockorder_release(g, 0, 0) == 0);
    CHECK(ios27_21j_lockorder_acquire(g, 1, 1) == 0);
    CHECK(ios27_21j_lockorder_acquire(g, 1, 2) == 0);
    CHECK(ios27_21j_lockorder_release(g, 1, 1) == 0);
    CHECK(ios27_21j_lockorder_release(g, 1, 2) == 0);
    CHECK(ios27_21j_lockorder_acquire(g, 0, 2) == 0);
    CHECK(ios27_21j_lockorder_acquire(g, 0, 0) == EDEADLK);
    CHECK(ios27_21j_lockorder_release(g, 0, 2) == 0);
    CHECK(ios27_21j_lockorder_release(g, 0, 2) == EPERM);
    CHECK(ios27_21j_lockorder_snapshot(g, &stats) == 0);
    CHECK(stats.edges == 2 && stats.cycles_rejected == 2);
    CHECK(stats.contention == 1 && stats.active == 0);
    CHECK(stats.acquisitions == stats.releases);
    CHECK(ios27_21j_lockorder_reset(g) == 0);
    CHECK(ios27_21j_lockorder_edge(g, 0, 1, &edge) == 0 && !edge);
    CHECK(ios27_21j_lockorder_acquire(g, 0, 2) == 0);
    CHECK(ios27_21j_lockorder_acquire(g, 0, 0) == 0);
    CHECK(ios27_21j_lockorder_release(g, 0, 0) == 0);
    CHECK(ios27_21j_lockorder_release(g, 0, 2) == 0);
    CHECK(ios27_21j_lockorder_destroy(g) == 0);
}
static void highest_bit(void) {
    ios27_21j_lockorder *g = NULL;
    ios27_21j_lockorder_stats stats;
    int edge;
    CHECK(ios27_21j_lockorder_create(&g, IOS27_21J_GRAPH_MAX_LOCKS, 2) == 0);
    CHECK(ios27_21j_lockorder_acquire(g, 0, 63) == 0);
    CHECK(ios27_21j_lockorder_acquire(g, 0, 0) == 0);
    CHECK(ios27_21j_lockorder_release(g, 0, 0) == 0);
    CHECK(ios27_21j_lockorder_release(g, 0, 63) == 0);
    CHECK(ios27_21j_lockorder_acquire(g, 1, 0) == 0);
    CHECK(ios27_21j_lockorder_acquire(g, 1, 63) == EDEADLK);
    CHECK(ios27_21j_lockorder_release(g, 1, 0) == 0);
    CHECK(ios27_21j_lockorder_edge(g, 63, 0, &edge) == 0 && edge);
    CHECK(ios27_21j_lockorder_snapshot(g, &stats) == 0);
    CHECK(stats.active == 0 && stats.cycles_rejected == 1);
    CHECK(ios27_21j_lockorder_destroy(g) == 0);
}
typedef struct { ios27_21j_lockorder *g; unsigned actor; } argument;
static void *ordered_worker(void *pointer) {
    argument *a = pointer;
    for (unsigned n = 0; n < ROUNDS; ++n) {
        unsigned locks[HELD_PER_ROUND];
        for (unsigned i = 0; i < HELD_PER_ROUND; ++i)
            locks[i] = i * 4u + (unsigned)(scramble(((uint64_t)n << 8) |
                     ((uint64_t)a->actor << 4) | i) % 4u);
        for (unsigned i = 0; i < HELD_PER_ROUND; ++i) {
            int rc = EBUSY;
            for (unsigned spin = 0; spin < 1000000u && rc == EBUSY; ++spin) {
                rc = ios27_21j_lockorder_acquire(a->g, a->actor, locks[i]);
                if (rc == EBUSY) sched_yield();
            }
            CHECK(rc == 0);
        }
        for (unsigned i = HELD_PER_ROUND; i > 0; --i)
            CHECK(ios27_21j_lockorder_release(a->g, a->actor,
                                                locks[i - 1u]) == 0);
    }
    return NULL;
}
static void concurrent_acyclic_stress(void) {
    ios27_21j_lockorder *g = NULL;
    ios27_21j_lockorder_stats stats;
    pthread_t threads[ACTORS];
    argument args[ACTORS];
    CHECK(ios27_21j_lockorder_create(&g, LOCKS, ACTORS) == 0);
    for (unsigned i = 0; i < ACTORS; ++i) {
        args[i] = (argument){g, i};
        CHECK(pthread_create(&threads[i], NULL, ordered_worker, &args[i]) == 0);
    }
    for (unsigned i = 0; i < ACTORS; ++i)
        CHECK(pthread_join(threads[i], NULL) == 0);
    CHECK(ios27_21j_lockorder_snapshot(g, &stats) == 0);
    CHECK(stats.active == 0 && stats.peak <= LOCKS);
    CHECK(stats.acquisitions == ACTORS * ROUNDS * HELD_PER_ROUND);
    CHECK(stats.releases == stats.acquisitions);
    CHECK(stats.cycles_rejected == 0 && stats.edges <= 120);
    CHECK(stats.edges > 0);
    CHECK(ios27_21j_lockorder_reset(g) == 0);
    CHECK(ios27_21j_lockorder_snapshot(g, &stats) == 0);
    CHECK(stats.edges == 0 && stats.resets == 1);
    CHECK(ios27_21j_lockorder_destroy(g) == 0);
}
int main(void) {
    errors_and_cycle();
    highest_bit();
    concurrent_acyclic_stress();
    printf("phase21j lock order: 96000 acquisitions, deadlock-cycle and quota diagnostics; PASS\n");
    return 0;
}
