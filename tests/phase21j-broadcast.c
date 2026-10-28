#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21c_sync.h"
#include "ios27/libsystem21j_diagnostics.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21j broadcast:%d %s\n", __LINE__, #x); abort(); \
} } while (0)
#define OK(x) CHECK((x) == 0)
enum { WAITERS = 32, ROUNDS = 128 };
typedef struct {
    ios27_21c_runtime *runtime;
    ios27_21c_handle mutex, changed, all_ready;
    ios27_21j_diagnostics *diag;
    unsigned ready, epoch, successful_wakes;
} shared;
typedef struct { shared *r; unsigned actor; } worker_arg;
static void *waiter(void *value) {
    worker_arg *a = value;
    shared *r = a->r;
    ios27_21j_lease lease;
    OK(ios27_21j_acquire(r->diag, &lease));
    for (unsigned epoch = 0; epoch < ROUNDS; ++epoch) {
        OK(ios27_21c_mutex_lock(r->runtime, r->mutex));
        CHECK(r->epoch == epoch);
        ++r->ready;
        if (r->ready == WAITERS)
            OK(ios27_21c_cond_signal(r->runtime, r->all_ready));
        while (r->epoch == epoch)
            OK(ios27_21c_cond_wait(r->runtime, r->changed, r->mutex));
        CHECK(r->epoch == epoch + 1u);
        ++r->successful_wakes;
        OK(ios27_21j_record(r->diag, IOS27_21J_WAIT,
                             a->actor, 0, epoch));
        OK(ios27_21c_mutex_unlock(r->runtime, r->mutex));
    }
    OK(ios27_21j_release(r->diag, lease));
    return NULL;
}
int main(void) {
    shared r = {0};
    pthread_t threads[WAITERS];
    worker_arg args[WAITERS];
    ios27_21j_snapshot_data stats;
    size_t count, in_use = 0;
    ios27_21j_event events[IOS27_21J_TRACE_CAPACITY];
    uint64_t first, last;
    OK(ios27_21c_runtime_create(&r.runtime, 3));
    OK(ios27_21c_sync_create(r.runtime, IOS27_21C_ERRORCHECK_MUTEX, &r.mutex));
    OK(ios27_21c_sync_create(r.runtime, IOS27_21C_CONDITION, &r.changed));
    OK(ios27_21c_sync_create(r.runtime, IOS27_21C_CONDITION, &r.all_ready));
    OK(ios27_21j_create(&r.diag, WAITERS, UINT64_C(0x21b00a), 0));
    for (unsigned actor = 0; actor < WAITERS; ++actor) {
        args[actor] = (worker_arg){&r, actor};
        OK(pthread_create(&threads[actor], NULL, waiter, &args[actor]));
    }
    for (unsigned epoch = 0; epoch < ROUNDS; ++epoch) {
        OK(ios27_21c_mutex_lock(r.runtime, r.mutex));
        while (r.ready < WAITERS)
            OK(ios27_21c_cond_wait(r.runtime, r.all_ready, r.mutex));
        CHECK(r.ready == WAITERS && r.epoch == epoch);
        if (epoch == 0) {
            OK(ios27_21j_snapshot(r.diag, &stats));
            CHECK(stats.active == WAITERS && stats.peak == WAITERS);
            CHECK(ios27_21j_destroy(r.diag) == EBUSY);
        }
        r.ready = 0;
        ++r.epoch;
        OK(ios27_21c_cond_broadcast(r.runtime, r.changed));
        OK(ios27_21j_record(r.diag, IOS27_21J_CHECKPOINT,
                             WAITERS, 0, epoch));
        OK(ios27_21c_mutex_unlock(r.runtime, r.mutex));
    }
    for (unsigned actor = 0; actor < WAITERS; ++actor)
        OK(pthread_join(threads[actor], NULL));
    CHECK(r.epoch == ROUNDS && r.ready == 0);
    CHECK(r.successful_wakes == ROUNDS * WAITERS);
    OK(ios27_21j_snapshot(r.diag, &stats));
    CHECK(stats.active == 0 && stats.peak == WAITERS);
    CHECK(stats.leases_created == WAITERS && stats.leases_released == WAITERS);
    CHECK(stats.waits == ROUNDS * WAITERS);
    CHECK(stats.next_sequence == ROUNDS * (WAITERS + 1u) + WAITERS * 2u);
    OK(ios27_21j_export(r.diag, events, IOS27_21J_TRACE_CAPACITY,
                          &count, &first, &last));
    CHECK(count == IOS27_21J_TRACE_CAPACITY && last == stats.next_sequence);
    for (size_t offset = 0; offset < count; ++offset)
        CHECK(events[offset].sequence == first + offset);
    ios27_21c_usage(r.runtime, &in_use);
    CHECK(in_use == 3);
    OK(ios27_21c_sync_destroy(r.runtime, r.changed));
    OK(ios27_21c_sync_destroy(r.runtime, r.all_ready));
    OK(ios27_21c_sync_destroy(r.runtime, r.mutex));
    OK(ios27_21c_runtime_destroy(r.runtime));
    OK(ios27_21j_destroy(r.diag));
    printf("phase21j broadcast: %u bounded generations, %u synchronized wakeups, 0 leaked leases; PASS\n",
           ROUNDS, WAITERS * ROUNDS);
    return 0;
}
