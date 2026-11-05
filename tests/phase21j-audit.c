#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21j_diagnostics.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21j audit:%d %s\n", __LINE__, #x); abort(); \
} } while (0)
enum { WORKERS = 8, LOOPS = 2500, AUDITS = 30000 };
typedef struct {
    ios27_21j_diagnostics *runtime;
    pthread_barrier_t start;
    atomic_uint complete;
    atomic_uint audit_calls;
} state;
typedef struct { state *shared; unsigned actor; } worker_arg;
static void *writer(void *value) {
    worker_arg *arg = value;
    state *s = arg->shared;
    int rc = pthread_barrier_wait(&s->start);
    CHECK(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    for (unsigned i = 0; i < LOOPS; ++i) {
        ios27_21j_lease lease;
        rc = EAGAIN;
        for (unsigned retry = 0; retry < 100000u && rc == EAGAIN; ++retry) {
            rc = ios27_21j_acquire(s->runtime, &lease);
            if (rc == EAGAIN) sched_yield();
        }
        CHECK(rc == 0);
        CHECK(ios27_21j_probe(s->runtime, 5, arg->actor, i) == 0);
        CHECK(ios27_21j_note_contention(s->runtime, arg->actor,
                (uint64_t)i * 10u + arg->actor) == 0);
        CHECK(ios27_21j_record(s->runtime, IOS27_21J_WAIT,
                               arg->actor, 0, i) == 0);
        CHECK(ios27_21j_release(s->runtime, lease) == 0);
    }
    atomic_fetch_add_explicit(&s->complete, 1u, memory_order_release);
    return NULL;
}
static void *reader(void *value) {
    state *s = value;
    int rc = pthread_barrier_wait(&s->start);
    CHECK(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    for (unsigned i = 0; i < AUDITS ||
         atomic_load_explicit(&s->complete, memory_order_acquire) < WORKERS;
         ++i) {
        ios27_21j_audit_data audit;
        ios27_21j_snapshot_data stats;
        CHECK(ios27_21j_audit(s->runtime, &audit) == 0);
        CHECK(audit.violations == 0);
        CHECK(audit.occupied <= WORKERS && audit.retired == 0);
        CHECK(audit.resident <= IOS27_21J_TRACE_CAPACITY);
        CHECK(ios27_21j_snapshot(s->runtime, &stats) == 0);
        CHECK(stats.capacity == WORKERS);
        atomic_fetch_add_explicit(&s->audit_calls, 1u,
                                  memory_order_relaxed);
    }
    return NULL;
}
int main(void) {
    state s = {0};
    ios27_21j_audit_data audit;
    ios27_21j_snapshot_data stats;
    pthread_t workers[WORKERS], auditor;
    worker_arg arguments[WORKERS];
    CHECK(ios27_21j_create(&s.runtime, WORKERS, 5, 0) == 0);
    CHECK(ios27_21j_audit(NULL, &audit) == EINVAL);
    CHECK(ios27_21j_audit(s.runtime, NULL) == EINVAL);
    CHECK(ios27_21j_audit(s.runtime, &audit) == 0);
    CHECK(audit.occupied == 0 && audit.retired == 0 &&
          audit.resident == 0 && audit.first == 0 && audit.last == 0);
    CHECK(pthread_barrier_init(&s.start, NULL, WORKERS + 1u) == 0);
    for (unsigned i = 0; i < WORKERS; ++i) {
        arguments[i] = (worker_arg){&s, i};
        CHECK(pthread_create(&workers[i], NULL, writer,
                             &arguments[i]) == 0);
    }
    CHECK(pthread_create(&auditor, NULL, reader, &s) == 0);
    for (unsigned i = 0; i < WORKERS; ++i)
        CHECK(pthread_join(workers[i], NULL) == 0);
    CHECK(pthread_join(auditor, NULL) == 0);
    CHECK(pthread_barrier_destroy(&s.start) == 0);
    CHECK(atomic_load(&s.audit_calls) >= AUDITS);
    CHECK(ios27_21j_audit(s.runtime, &audit) == 0);
    CHECK(ios27_21j_snapshot(s.runtime, &stats) == 0);
    CHECK(audit.occupied == 0 && audit.retired == 0);
    CHECK(audit.resident == IOS27_21J_TRACE_CAPACITY);
    CHECK(audit.last == stats.next_sequence &&
          audit.first == audit.last - audit.resident + 1u);
    CHECK(stats.probes == WORKERS * LOOPS);
    CHECK(stats.waits == WORKERS * LOOPS);
    CHECK(stats.contention == WORKERS * LOOPS);
    CHECK(stats.leases_created == WORKERS * LOOPS);
    CHECK(stats.leases_released == stats.leases_created);
    CHECK(stats.active == 0 && stats.peak <= WORKERS);
    CHECK(stats.dropped == stats.next_sequence - audit.resident);
    CHECK(ios27_21j_destroy(s.runtime) == 0);
    printf("phase21j audit: %u threaded lease lifecycles and %u+ live invariant audits, clean trace and counters; PASS\n",
           WORKERS * LOOPS, AUDITS);
    return 0;
}
