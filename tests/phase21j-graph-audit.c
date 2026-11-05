#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21j_lockorder.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21j graph audit:%d %s\n", __LINE__, #x); abort(); \
} } while (0)
enum { WORKERS = 8, LOCKS = 16, LOOPS = 2000, AUDITS = 10000 };
typedef struct {
    ios27_21j_lockorder *g;
    pthread_barrier_t gate;
    atomic_uint done;
} shared;
typedef struct { shared *state; unsigned actor; } argument;
static void *writer(void *value) {
    argument *arg = value;
    shared *s = arg->state;
    int rc = pthread_barrier_wait(&s->gate);
    CHECK(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    for (unsigned i = 0; i < LOOPS; ++i) {
        unsigned first = (i + arg->actor) % 8u;
        unsigned second = 8u + ((i * 7u + arg->actor) % 8u);
        for (unsigned lock_index = 0; lock_index < 2; ++lock_index) {
            unsigned lock = lock_index ? second : first;
            rc = EBUSY;
            for (unsigned spin = 0; spin < 1000000u && rc == EBUSY;
                 ++spin) {
                rc = ios27_21j_lockorder_acquire(s->g, arg->actor, lock);
                if (rc == EBUSY) sched_yield();
            }
            CHECK(rc == 0);
        }
        if ((i & 31u) == 0u) {
            ios27_21j_graph_audit audit;
            CHECK(ios27_21j_lockorder_audit(s->g, &audit) == 0);
            CHECK(audit.violations == 0 && audit.owned <= LOCKS &&
                  audit.edges <= LOCKS * (LOCKS - 1u));
        }
        CHECK(ios27_21j_lockorder_release(s->g, arg->actor, second) == 0);
        CHECK(ios27_21j_lockorder_release(s->g, arg->actor, first) == 0);
    }
    atomic_fetch_add_explicit(&s->done, 1u, memory_order_release);
    return NULL;
}
static void *auditor(void *value) {
    shared *s = value;
    int rc = pthread_barrier_wait(&s->gate);
    CHECK(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    for (unsigned i = 0; i < AUDITS ||
         atomic_load_explicit(&s->done, memory_order_acquire) < WORKERS;
         ++i) {
        ios27_21j_graph_audit audit;
        CHECK(ios27_21j_lockorder_audit(s->g, &audit) == 0);
        CHECK(audit.examined_actors == WORKERS);
        CHECK(audit.waiting == 0 && audit.violations == 0);
    }
    return NULL;
}
static void active_wait_reparenting(void) {
    ios27_21j_lockorder *g = NULL;
    ios27_21j_graph_audit audit;
    CHECK(ios27_21j_lockorder_create(&g, 3, 3) == 0);
    CHECK(ios27_21j_lockorder_audit(NULL, &audit) == EINVAL);
    CHECK(ios27_21j_lockorder_audit(g, NULL) == EINVAL);
    CHECK(ios27_21j_lockorder_acquire(g, 0, 0) == 0);
    CHECK(ios27_21j_lockorder_acquire(g, 1, 1) == 0);
    CHECK(ios27_21j_lockorder_wait(g, 0, 1) == 0);
    CHECK(ios27_21j_lockorder_audit(g, &audit) == 0);
    CHECK(audit.owned == 2 && audit.waiting == 1 && audit.edges == 0);
    CHECK(ios27_21j_lockorder_release(g, 1, 1) == 0);
    CHECK(ios27_21j_lockorder_acquire(g, 2, 1) == 0);
    CHECK(ios27_21j_lockorder_audit(g, &audit) == 0);
    CHECK(audit.owned == 2 && audit.waiting == 1);
    CHECK(ios27_21j_lockorder_cancel_wait(g, 0, 1) == 0);
    CHECK(ios27_21j_lockorder_release(g, 0, 0) == 0);
    CHECK(ios27_21j_lockorder_release(g, 2, 1) == 0);
    CHECK(ios27_21j_lockorder_audit(g, &audit) == 0);
    CHECK(audit.owned == 0 && audit.waiting == 0);
    CHECK(ios27_21j_lockorder_destroy(g) == 0);
}
int main(void) {
    shared s = {0};
    pthread_t threads[WORKERS], reader;
    argument args[WORKERS];
    ios27_21j_graph_audit audit;
    ios27_21j_lockorder_stats stats;
    active_wait_reparenting();
    CHECK(ios27_21j_lockorder_create(&s.g, LOCKS, WORKERS) == 0);
    CHECK(pthread_barrier_init(&s.gate, NULL, WORKERS + 1u) == 0);
    for (unsigned i = 0; i < WORKERS; ++i) {
        args[i] = (argument){&s, i};
        CHECK(pthread_create(&threads[i], NULL, writer, &args[i]) == 0);
    }
    CHECK(pthread_create(&reader, NULL, auditor, &s) == 0);
    for (unsigned i = 0; i < WORKERS; ++i)
        CHECK(pthread_join(threads[i], NULL) == 0);
    CHECK(pthread_join(reader, NULL) == 0);
    CHECK(pthread_barrier_destroy(&s.gate) == 0);
    CHECK(ios27_21j_lockorder_audit(s.g, &audit) == 0);
    CHECK(audit.owned == 0 && audit.waiting == 0 && audit.edges > 0);
    CHECK(ios27_21j_lockorder_snapshot(s.g, &stats) == 0);
    CHECK(stats.acquisitions == WORKERS * LOOPS * 2u);
    CHECK(stats.releases == stats.acquisitions);
    CHECK(stats.active == 0 && stats.waiting == 0 &&
          stats.cycles_rejected == 0);
    CHECK(ios27_21j_lockorder_destroy(s.g) == 0);
    printf("phase21j graph audit: %u concurrent owner transitions and %u+ validated DAG/wait-for snapshots; PASS\n",
           WORKERS * LOOPS * 2u, AUDITS);
    return 0;
}
