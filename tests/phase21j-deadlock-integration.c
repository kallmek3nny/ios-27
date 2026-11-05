#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21c_sync.h"
#include "ios27/libsystem21j_diagnostics.h"
#include "ios27/libsystem21j_lockorder.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21j deadlock integration:%d %s\n", __LINE__, #x); abort(); \
} } while (0)
#define OK(x) CHECK((x) == 0)
enum { TWO_ROUNDS = 512, THREE_ROUNDS = 256, MAX_ACTORS = 3 };
typedef struct deadlock_state deadlock_state;
typedef struct { deadlock_state *state; unsigned actor; int result; } actor_arg;
struct deadlock_state {
    ios27_21c_runtime *rt;
    ios27_21c_handle mutexes[MAX_ACTORS];
    ios27_21j_lockorder *graph;
    ios27_21j_diagnostics *diag;
    pthread_barrier_t barrier;
    unsigned actors;
};
static void *worker(void *value) {
    actor_arg *a = value;
    deadlock_state *s = a->state;
    unsigned own = a->actor, other = (own + 1u) % s->actors;
    /* Instrument the known-free logical lock before the native bridge.
       Never call a blocking native lock if lockorder denies ownership. */
    OK(ios27_21j_lockorder_acquire(s->graph, own, own));
    OK(ios27_21c_mutex_lock(s->rt, s->mutexes[own]));
    int barrier = pthread_barrier_wait(&s->barrier);
    CHECK(barrier == 0 || barrier == PTHREAD_BARRIER_SERIAL_THREAD);
    a->result = ios27_21j_lockorder_wait(s->graph, own, other);
    CHECK(a->result == 0 || a->result == EDEADLK);
    OK(ios27_21j_record(s->diag, IOS27_21J_CHECKPOINT, own,
                         a->result, other));
    if (a->result == 0) {
        /* At most N-1 actors enter this blocking call; the sole rejected
           actor releases its own mutex and breaks the wait-for cycle. */
        OK(ios27_21c_mutex_lock(s->rt, s->mutexes[other]));
        OK(ios27_21j_lockorder_cancel_wait(s->graph, own, other));
        OK(ios27_21j_lockorder_acquire(s->graph, own, other));
        OK(ios27_21j_lockorder_release(s->graph, own, other));
        OK(ios27_21c_mutex_unlock(s->rt, s->mutexes[other]));
    }
    OK(ios27_21j_lockorder_release(s->graph, own, own));
    OK(ios27_21c_mutex_unlock(s->rt, s->mutexes[own]));
    return NULL;
}
static void cycle_round(unsigned actors) {
    deadlock_state s = {0};
    pthread_t workers[MAX_ACTORS];
    actor_arg args[MAX_ACTORS];
    ios27_21j_lockorder_stats graph_stats;
    ios27_21j_snapshot_data diagnostic_stats;
    size_t used = 0, rejected = 0, permitted = 0;
    s.actors = actors;
    OK(ios27_21c_runtime_create(&s.rt, MAX_ACTORS));
    OK(ios27_21j_lockorder_create(&s.graph, actors, actors));
    OK(ios27_21j_create(&s.diag, actors, actors, 0));
    for (unsigned i = 0; i < actors; ++i)
        OK(ios27_21c_sync_create(s.rt, IOS27_21C_ERRORCHECK_MUTEX,
                                 &s.mutexes[i]));
    OK(pthread_barrier_init(&s.barrier, NULL, actors));
    for (unsigned i = 0; i < actors; ++i) {
        args[i] = (actor_arg){&s, i, -1};
        OK(pthread_create(&workers[i], NULL, worker, &args[i]));
    }
    for (unsigned i = 0; i < actors; ++i)
        OK(pthread_join(workers[i], NULL));
    for (unsigned i = 0; i < actors; ++i) {
        rejected += (unsigned)(args[i].result == EDEADLK);
        permitted += (unsigned)(args[i].result == 0);
    }
    CHECK(rejected == 1 && permitted == actors - 1u);
    OK(ios27_21j_lockorder_snapshot(s.graph, &graph_stats));
    CHECK(graph_stats.active == 0 && graph_stats.waiting == 0);
    CHECK(graph_stats.wait_cycles_rejected == 1);
    CHECK(graph_stats.cycles_rejected == 1 && graph_stats.edges <= actors - 1u);
    CHECK(graph_stats.acquisitions == 2u * actors - 1u);
    CHECK(graph_stats.acquisitions == graph_stats.releases);
    OK(ios27_21j_snapshot(s.diag, &diagnostic_stats));
    CHECK(diagnostic_stats.next_sequence == actors && diagnostic_stats.active == 0);
    size_t journal_rejections = 0;
    for (uint64_t seq = 1; seq <= actors; ++seq) {
        ios27_21j_event event;
        OK(ios27_21j_read(s.diag, seq, &event));
        CHECK(event.kind == IOS27_21J_CHECKPOINT);
        journal_rejections += (size_t)(event.result == EDEADLK);
    }
    CHECK(journal_rejections == 1);
    for (unsigned i = 0; i < actors; ++i)
        OK(ios27_21c_sync_destroy(s.rt, s.mutexes[i]));
    ios27_21c_usage(s.rt, &used);
    CHECK(used == 0);
    OK(pthread_barrier_destroy(&s.barrier));
    OK(ios27_21j_destroy(s.diag));
    OK(ios27_21j_lockorder_destroy(s.graph));
    OK(ios27_21c_runtime_destroy(s.rt));
}
static void wait_state_errors(void) {
    ios27_21j_lockorder *graph = NULL;
    ios27_21j_lockorder_stats stats;
    CHECK(ios27_21j_lockorder_create(&graph, 2, 2) == 0);
    CHECK(ios27_21j_lockorder_wait(graph, 0, 1) == EAGAIN);
    CHECK(ios27_21j_lockorder_cancel_wait(graph, 0, 1) == EINVAL);
    CHECK(ios27_21j_lockorder_acquire(graph, 0, 1) == 0);
    CHECK(ios27_21j_lockorder_wait(graph, 1, 1) == 0);
    CHECK(ios27_21j_lockorder_wait(graph, 1, 1) == EALREADY);
    CHECK(ios27_21j_lockorder_acquire(graph, 1, 0) == EBUSY);
    CHECK(ios27_21j_lockorder_destroy(graph) == EBUSY);
    CHECK(ios27_21j_lockorder_reset(graph) == EBUSY);
    CHECK(ios27_21j_lockorder_release(graph, 0, 1) == 0);
    CHECK(ios27_21j_lockorder_snapshot(graph, &stats) == 0);
    CHECK(stats.waiting == 1 && stats.active == 0);
    CHECK(ios27_21j_lockorder_destroy(graph) == EBUSY);
    CHECK(ios27_21j_lockorder_cancel_wait(graph, 1, 1) == 0);
    CHECK(ios27_21j_lockorder_cancel_wait(graph, 1, 1) == EINVAL);
    CHECK(ios27_21j_lockorder_reset(graph) == 0);
    CHECK(ios27_21j_lockorder_destroy(graph) == 0);
}
int main(void) {
    wait_state_errors();
    for (unsigned i = 0; i < TWO_ROUNDS; ++i) cycle_round(2);
    for (unsigned i = 0; i < THREE_ROUNDS; ++i) cycle_round(3);
    printf("phase21j deadlock integration: %u two-way and %u three-way native mutex cycles rejected; PASS\n",
           TWO_ROUNDS, THREE_ROUNDS);
    return 0;
}
