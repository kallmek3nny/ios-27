#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21c_sync.h"
#include "ios27/libsystem21j_diagnostics.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21j timeout-races:%d %s\n", __LINE__, #x); abort(); \
} } while (0)
#define OK(x) CHECK((x) == 0)
enum { CYCLES = 768, GUARANTEED_EXPIRED = CYCLES / 3u };
typedef struct {
    ios27_21c_runtime *runtime;
    ios27_21c_handle mutex, condition;
    ios27_21j_diagnostics *diag;
    pthread_barrier_t gate;
    unsigned epoch, mode, published;
    int wait_result, predicate_after_wait;
} round_state;
static void *waiting(void *pointer) {
    round_state *r = pointer;
    struct timespec deadline;
    int rc;
    OK(ios27_21c_mutex_lock(r->runtime, r->mutex));
    OK(clock_gettime(CLOCK_MONOTONIC, &deadline) ? errno : 0);
    if (r->mode == 0u) --deadline.tv_sec;
    else if (r->mode == 1u) deadline.tv_sec += 5;
    else {
        deadline.tv_nsec += 10000L;
        if (deadline.tv_nsec >= 1000000000L) {
            ++deadline.tv_sec;
            deadline.tv_nsec -= 1000000000L;
        }
    }
    rc = pthread_barrier_wait(&r->gate);
    CHECK(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    for (;;) {
        if (r->published) { r->wait_result = 0; break; }
        rc = ios27_21c_cond_timedwait(r->runtime, r->condition, r->mutex,
                                       IOS27_21C_MONOTONIC, &deadline);
        CHECK(rc == 0 || rc == ETIMEDOUT);
        if (rc == ETIMEDOUT) { r->wait_result = rc; break; }
    }
    r->predicate_after_wait = (int)r->published;
    CHECK(r->wait_result == 0 || r->wait_result == ETIMEDOUT);
    OK(ios27_21j_record(r->diag,
        r->wait_result == ETIMEDOUT ? IOS27_21J_TIMEOUT : IOS27_21J_WAIT,
        0, r->wait_result, r->epoch));
    OK(ios27_21c_mutex_unlock(r->runtime, r->mutex));
    return NULL;
}
static void *signaling(void *pointer) {
    round_state *r = pointer;
    int rc = pthread_barrier_wait(&r->gate);
    CHECK(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    OK(ios27_21c_mutex_lock(r->runtime, r->mutex));
    CHECK(r->published == 0);
    r->published = 1;
    OK(ios27_21c_cond_signal(r->runtime, r->condition));
    OK(ios27_21j_record(r->diag, IOS27_21J_CHECKPOINT, 1, 0, r->epoch));
    OK(ios27_21c_mutex_unlock(r->runtime, r->mutex));
    return NULL;
}
static void run(void) {
    ios27_21c_runtime *runtime = NULL;
    ios27_21c_handle mutex, condition;
    ios27_21j_diagnostics *diag = NULL;
    ios27_21j_snapshot_data snapshot;
    unsigned timed_out = 0, notified = 0;
    size_t in_use = 0;
    OK(ios27_21c_runtime_create(&runtime, 2));
    OK(ios27_21c_sync_create(runtime, IOS27_21C_ERRORCHECK_MUTEX, &mutex));
    OK(ios27_21c_sync_create(runtime, IOS27_21C_CONDITION, &condition));
    OK(ios27_21j_create(&diag, 2, 123, 0));
    for (unsigned epoch = 0; epoch < CYCLES; ++epoch) {
        pthread_t waiter, signaler;
        round_state r = {
            .runtime = runtime, .mutex = mutex, .condition = condition,
            .diag = diag, .epoch = epoch, .mode = epoch % 3u,
            .wait_result = -1, .predicate_after_wait = -1,
        };
        OK(pthread_barrier_init(&r.gate, NULL, 2));
        OK(pthread_create(&waiter, NULL, waiting, &r));
        OK(pthread_create(&signaler, NULL, signaling, &r));
        OK(pthread_join(waiter, NULL));
        OK(pthread_join(signaler, NULL));
        OK(pthread_barrier_destroy(&r.gate));
        CHECK(r.published == 1);
        CHECK((r.wait_result == 0 && r.predicate_after_wait == 1) ||
              (r.wait_result == ETIMEDOUT &&
               (r.predicate_after_wait == 0 || r.predicate_after_wait == 1)));
        if (r.mode == 0u) CHECK(r.wait_result == ETIMEDOUT);
        if (r.mode == 1u) CHECK(r.wait_result == 0);
        timed_out += (unsigned)(r.wait_result == ETIMEDOUT);
        notified += (unsigned)(r.wait_result == 0);
        if ((epoch & 127u) == 0u) {
            OK(ios27_21j_snapshot(diag, &snapshot));
            CHECK(snapshot.timeouts == timed_out);
            CHECK(snapshot.waits == notified);
            CHECK(snapshot.next_sequence == 2u * (epoch + 1u));
        }
    }
    CHECK(timed_out >= GUARANTEED_EXPIRED && notified >= GUARANTEED_EXPIRED);
    CHECK(timed_out + notified == CYCLES);
    OK(ios27_21j_snapshot(diag, &snapshot));
    CHECK(snapshot.next_sequence == CYCLES * 2u);
    CHECK(snapshot.timeouts == timed_out && snapshot.waits == notified);
    CHECK(snapshot.active == 0 && snapshot.leases_created == 0);
    ios27_21c_usage(runtime, &in_use);
    CHECK(in_use == 2);
    OK(ios27_21c_sync_destroy(runtime, condition));
    OK(ios27_21c_sync_destroy(runtime, mutex));
    OK(ios27_21c_runtime_destroy(runtime));
    OK(ios27_21j_destroy(diag));
    printf("phase21j timeout-races: %u mutex-reacquired condition races, %u expired and %u signaled; PASS\n",
           CYCLES, timed_out, notified);
}
int main(void) { run(); return 0; }
