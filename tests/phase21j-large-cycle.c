#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21j_lockorder.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21j large-cycle:%d %s\n", __LINE__, #x); abort(); \
} } while (0)
#define OK(x) CHECK((x) == 0)
enum { ACTORS = 64, CYCLES = 16 };
typedef struct state state;
typedef struct { state *s; unsigned actor; int result; } arg;
struct state {
    ios27_21j_lockorder *graph;
    pthread_mutex_t native[ACTORS];
    pthread_barrier_t aligned;
};
static void *participant(void *value) {
    arg *a = value;
    state *s = a->s;
    unsigned own = a->actor, target = (own + 1u) % ACTORS;
    OK(ios27_21j_lockorder_acquire(s->graph, own, own));
    OK(pthread_mutex_lock(&s->native[own]));
    int rc = pthread_barrier_wait(&s->aligned);
    CHECK(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    a->result = ios27_21j_lockorder_wait(s->graph, own, target);
    CHECK(a->result == 0 || a->result == EDEADLK);
    if (!a->result) {
        /* The single rejected edge breaks the ring. Ownership then
           unwinds in finite order, without timed sleeps or polling. */
        OK(pthread_mutex_lock(&s->native[target]));
        OK(ios27_21j_lockorder_cancel_wait(s->graph, own, target));
        OK(ios27_21j_lockorder_acquire(s->graph, own, target));
        OK(ios27_21j_lockorder_release(s->graph, own, target));
        OK(pthread_mutex_unlock(&s->native[target]));
    }
    OK(ios27_21j_lockorder_release(s->graph, own, own));
    OK(pthread_mutex_unlock(&s->native[own]));
    return NULL;
}
int main(void) {
    state s = {0};
    pthread_t threads[ACTORS];
    arg args[ACTORS];
    ios27_21j_lockorder_stats stats;
    OK(ios27_21j_lockorder_create(&s.graph, ACTORS, ACTORS));
    for (unsigned actor = 0; actor < ACTORS; ++actor)
        OK(pthread_mutex_init(&s.native[actor], NULL));
    for (unsigned epoch = 0; epoch < CYCLES; ++epoch) {
        unsigned rejected = 0, accepted = 0;
        OK(pthread_barrier_init(&s.aligned, NULL, ACTORS));
        for (unsigned actor = 0; actor < ACTORS; ++actor) {
            args[actor] = (arg){&s, actor, -1};
            OK(pthread_create(&threads[actor], NULL, participant,
                               &args[actor]));
        }
        for (unsigned actor = 0; actor < ACTORS; ++actor)
            OK(pthread_join(threads[actor], NULL));
        for (unsigned actor = 0; actor < ACTORS; ++actor) {
            rejected += (unsigned)(args[actor].result == EDEADLK);
            accepted += (unsigned)(args[actor].result == 0);
        }
        CHECK(rejected == 1u && accepted == ACTORS - 1u);
        OK(ios27_21j_lockorder_snapshot(s.graph, &stats));
        CHECK(stats.active == 0 && stats.waiting == 0);
        CHECK(stats.peak == ACTORS);
        CHECK(stats.wait_cycles_rejected == epoch + 1u);
        CHECK(stats.acquisitions == (epoch + 1u) * (ACTORS * 2u - 1u));
        CHECK(stats.acquisitions == stats.releases);
        OK(ios27_21j_lockorder_reset(s.graph));
        OK(pthread_barrier_destroy(&s.aligned));
    }
    OK(ios27_21j_lockorder_snapshot(s.graph, &stats));
    CHECK(stats.edges == 0 && stats.resets == CYCLES);
    for (unsigned actor = 0; actor < ACTORS; ++actor)
        OK(pthread_mutex_destroy(&s.native[actor]));
    OK(ios27_21j_lockorder_destroy(s.graph));
    printf("phase21j large-cycle: %u fully contended 64-thread deadlock rings safely unwound; PASS\n",
           CYCLES);
    return 0;
}
