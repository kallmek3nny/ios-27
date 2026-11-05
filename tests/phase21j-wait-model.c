#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21j_lockorder.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21j wait-model:%d %s\n", __LINE__, #x); abort(); \
} } while (0)
enum { ACTORS = 8, STEPS = 120000, EPOCH = 256 };
typedef struct {
    ios27_21j_lockorder *graph;
    int owns[ACTORS];
    int waiting[ACTORS];
    uint64_t acquires, releases, wait_cycles, self_cycles;
    uint64_t contention, resets;
} oracle;
static uint64_t next_random(uint64_t *seed) {
    *seed += UINT64_C(0x9e3779b97f4a7c15);
    uint64_t value = *seed;
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}
static int cycle_if_wait(const oracle *o, unsigned actor, unsigned target) {
    unsigned current = target;
    for (unsigned hops = 0; hops < ACTORS; ++hops) {
        if (current == actor) return 1;
        int next = o->waiting[current];
        if (next < 0) break;
        if (!o->owns[next]) break;
        current = (unsigned)next;
    }
    return 0;
}
static void check(oracle *o) {
    ios27_21j_lockorder_stats s;
    size_t held = 0, waiting = 0;
    for (unsigned i = 0; i < ACTORS; ++i) {
        held += (size_t)o->owns[i];
        waiting += (size_t)(o->waiting[i] >= 0);
    }
    CHECK(ios27_21j_lockorder_snapshot(o->graph, &s) == 0);
    CHECK(s.active == held && s.waiting == waiting && s.edges == 0);
    CHECK(s.acquisitions == o->acquires && s.releases == o->releases);
    CHECK(s.wait_cycles_rejected == o->wait_cycles);
    CHECK(s.cycles_rejected == o->wait_cycles + o->self_cycles);
    CHECK(s.contention == o->contention);
    CHECK(s.resets == o->resets);
    CHECK(s.peak <= ACTORS);
}
static void cleanup_epoch(oracle *o) {
    for (unsigned i = 0; i < ACTORS; ++i) {
        if (o->waiting[i] >= 0) {
            CHECK(ios27_21j_lockorder_cancel_wait(o->graph, i,
                    (uint32_t)o->waiting[i]) == 0);
            o->waiting[i] = -1;
        }
    }
    for (unsigned i = 0; i < ACTORS; ++i) {
        if (o->owns[i]) {
            CHECK(ios27_21j_lockorder_release(o->graph, i, i) == 0);
            o->owns[i] = 0;
            ++o->releases;
        }
    }
    CHECK(ios27_21j_lockorder_reset(o->graph) == 0);
    ++o->resets;
    check(o);
}
static void run(void) {
    oracle o = {0};
    uint64_t seed = UINT64_C(0x210b0a22);
    CHECK(ios27_21j_lockorder_create(&o.graph, ACTORS, ACTORS) == 0);
    for (unsigned actor = 0; actor < ACTORS; ++actor)
        o.waiting[actor] = -1;
    for (unsigned step = 0; step < STEPS; ++step) {
        unsigned actor = (unsigned)(next_random(&seed) % ACTORS);
        unsigned target = (unsigned)(next_random(&seed) % ACTORS);
        unsigned action = (unsigned)(next_random(&seed) % 5u);
        int actual, expected;
        if (action == 0) {
            /* Actors only acquire their own uniquely indexed mutex.
               Therefore no historical cross-lock edges can be created. */
            expected = o.waiting[actor] >= 0 ? EBUSY :
                       o.owns[actor] ? EDEADLK : 0;
            actual = ios27_21j_lockorder_acquire(o.graph, actor, actor);
            CHECK(actual == expected);
            if (!expected) { o.owns[actor] = 1; ++o.acquires; }
            if (expected == EDEADLK) ++o.self_cycles;
        } else if (action == 1) {
            expected = o.owns[actor] ? 0 : EPERM;
            actual = ios27_21j_lockorder_release(o.graph, actor, actor);
            CHECK(actual == expected);
            if (!expected) { o.owns[actor] = 0; ++o.releases; }
        } else if (action == 2) {
            expected = o.waiting[actor] >= 0 ? EALREADY :
                       !o.owns[target] ? EAGAIN :
                       cycle_if_wait(&o, actor, target) ? EDEADLK : 0;
            actual = ios27_21j_lockorder_wait(o.graph, actor, target);
            CHECK(actual == expected);
            if (!expected) o.waiting[actor] = (int)target;
            if (expected == EDEADLK) ++o.wait_cycles;
        } else if (action == 3) {
            expected = o.waiting[actor] == (int)target ? 0 : EINVAL;
            actual = ios27_21j_lockorder_cancel_wait(o.graph, actor, target);
            CHECK(actual == expected);
            if (!expected) o.waiting[actor] = -1;
        } else {
            /* Probe contention against a currently owned target without
               acquiring it, thus preserving the single-lock oracle. */
            if (o.owns[target] && target != actor) {
                actual = ios27_21j_lockorder_acquire(o.graph, actor, target);
                CHECK(actual == EBUSY);
                if (o.waiting[actor] < 0) ++o.contention;
            } else {
                expected = o.owns[actor] ? EDEADLK :
                           o.waiting[actor] >= 0 ? EBUSY : 0;
                /* No-op legal read, never changes oracle ownership. */
                ios27_21j_lockorder_stats snapshot;
                CHECK(ios27_21j_lockorder_snapshot(o.graph, &snapshot) == 0);
                CHECK(snapshot.active <= ACTORS);
                (void)expected;
            }
        }
        if ((step % EPOCH) == EPOCH - 1u) cleanup_epoch(&o);
        if ((step & 511u) == 0) check(&o);
    }
    cleanup_epoch(&o);
    check(&o);
    CHECK(o.wait_cycles > 0 && o.self_cycles > 0 &&
          o.contention > 0 && o.acquires > 0);
    CHECK(ios27_21j_lockorder_destroy(o.graph) == 0);
    printf("phase21j wait-model: %u seeded ownership/wait transitions, %llu wait-for cycles, %llu recursive rejects; PASS\n",
           STEPS, (unsigned long long)o.wait_cycles,
           (unsigned long long)o.self_cycles);
}
int main(void) { run(); return 0; }
