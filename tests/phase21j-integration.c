#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21b_lifecycle.h"
#include "ios27/libsystem21c_sync.h"
#include "ios27/libsystem21e_tls.h"
#include "ios27/libsystem21j_diagnostics.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21j integration:%d: %s\n", __LINE__, #x); abort(); \
} } while (0)
#define OK(x) CHECK((x) == 0)
enum { PRODUCERS = 4, CONSUMERS = 4, WORKERS = 8,
       ITEMS = 2500, ROUNDS = 6, QUEUE_CAP = 17 };

typedef struct run_state run_state;
typedef struct {
    run_state *run;
    unsigned actor;
    ios27_21e_handle token;
    ios27_21j_lease lease;
} worker_args;
struct run_state {
    ios27_21b_runtime *threads;
    ios27_21c_runtime *sync;
    ios27_21e_runtime *tls;
    ios27_21j_diagnostics *diag;
    ios27_21e_handle key;
    ios27_21c_handle mutex, not_empty, not_full;
    uint32_t queue[QUEUE_CAP];
    unsigned head, tail, size, done, produced, consumed;
    uint64_t sum, seed;
    pthread_mutex_t gate;
    pthread_cond_t gate_changed;
    unsigned ready;
    int release;
    atomic_uint cleanups, destructors;
    worker_args args[WORKERS];
    ios27_21b_thread ids[WORKERS];
};
static void tls_destructor(void *value) {
    worker_args *a = value;
    CHECK(a && a->run);
    atomic_fetch_add_explicit(&a->run->destructors, 1u, memory_order_relaxed);
}
static void cleanup(void *value) {
    worker_args *a = value;
    atomic_fetch_add_explicit(&a->run->cleanups, 1u, memory_order_relaxed);
}
static void lock_queue(run_state *r, unsigned actor) {
    int rc = ios27_21c_mutex_trylock(r->sync, r->mutex);
    if (rc == EBUSY) {
        struct timespec before, after;
        uint64_t start, end;
        OK(clock_gettime(CLOCK_MONOTONIC, &before) ? errno : 0);
        OK(ios27_21c_mutex_lock(r->sync, r->mutex));
        OK(clock_gettime(CLOCK_MONOTONIC, &after) ? errno : 0);
        start = (uint64_t)before.tv_sec * UINT64_C(1000000000) +
                (uint64_t)before.tv_nsec;
        end = (uint64_t)after.tv_sec * UINT64_C(1000000000) +
              (uint64_t)after.tv_nsec;
        CHECK(end >= start);
        OK(ios27_21j_note_contention(r->diag, actor, end - start));
    } else CHECK(rc == 0);
}
static void note_probe(run_state *r, unsigned actor,
                       unsigned probe_actor, unsigned ordinal) {
    int rc = ios27_21j_probe(r->diag, 31u + (probe_actor & 1u),
                             probe_actor, ordinal);
    CHECK(rc == 0 || rc == ENOMEM);
    if ((ordinal & 127u) == 0)
        OK(ios27_21j_record(r->diag, IOS27_21J_CHECKPOINT, actor, rc, ordinal));
}
static void produce(worker_args *a) {
    run_state *r = a->run;
    for (unsigned i = 0; i < ITEMS; ++i) {
        note_probe(r, a->actor, a->actor, i);
        if ((i & 127u) == 0) CHECK(ios27_21e_cancel_checkpoint(r->tls) == 0);
        lock_queue(r, a->actor);
        while (r->size == QUEUE_CAP) {
            OK(ios27_21j_record(r->diag, IOS27_21J_WAIT, a->actor, 0, i));
            OK(ios27_21c_cond_wait(r->sync, r->not_full, r->mutex));
        }
        r->queue[r->tail] = a->actor * ITEMS + i + 1u;
        r->tail = (r->tail + 1u) % QUEUE_CAP;
        ++r->size;
        ++r->produced;
        OK(ios27_21c_cond_signal(r->sync, r->not_empty));
        OK(ios27_21c_mutex_unlock(r->sync, r->mutex));
    }
    lock_queue(r, a->actor);
    ++r->done;
    OK(ios27_21c_cond_broadcast(r->sync, r->not_empty));
    OK(ios27_21c_mutex_unlock(r->sync, r->mutex));
}
static void consume(worker_args *a) {
    run_state *r = a->run;
    unsigned ordinal = 0;
    for (;;) {
        lock_queue(r, a->actor);
        while (!r->size && r->done < PRODUCERS) {
            struct timespec deadline;
            int rc;
            OK(clock_gettime(CLOCK_MONOTONIC, &deadline) ? errno : 0);
            deadline.tv_nsec += 5000000L;
            if (deadline.tv_nsec >= 1000000000L) {
                ++deadline.tv_sec;
                deadline.tv_nsec -= 1000000000L;
            }
            OK(ios27_21j_record(r->diag, IOS27_21J_WAIT, a->actor, 0, ordinal));
            rc = ios27_21c_cond_timedwait(r->sync, r->not_empty, r->mutex,
                                           IOS27_21C_MONOTONIC, &deadline);
            CHECK(rc == 0 || rc == ETIMEDOUT);
            if (rc == ETIMEDOUT)
                OK(ios27_21j_record(r->diag, IOS27_21J_TIMEOUT,
                                     a->actor, rc, ordinal));
        }
        if (!r->size && r->done == PRODUCERS) {
            OK(ios27_21c_mutex_unlock(r->sync, r->mutex));
            break;
        }
        CHECK(r->size != 0);
        uint32_t value = r->queue[r->head];
        r->sum += value;
        r->head = (r->head + 1u) % QUEUE_CAP;
        --r->size;
        ++r->consumed;
        OK(ios27_21c_cond_signal(r->sync, r->not_full));
        OK(ios27_21c_mutex_unlock(r->sync, r->mutex));
        /* Item identity, not scheduling-dependent consumer identity,
           selects the failure tuple. */
        note_probe(r, a->actor, WORKERS + 1u, value);
        if ((ordinal & 127u) == 0)
            CHECK(ios27_21e_cancel_checkpoint(r->tls) == 0);
        ++ordinal;
    }
}
static void *worker(void *value) {
    worker_args *a = value;
    run_state *r = a->run;
    uint64_t scope = 0;
    /* Injected ENOMEM must not consume a lease; retry deterministically. */
    int acquired = 0;
    for (unsigned attempt = 0; attempt < 128u; ++attempt) {
        int rc = ios27_21j_acquire_injected(r->diag, 13, a->actor,
                                            attempt, &a->lease);
        CHECK(rc == 0 || rc == ENOMEM);
        if (!rc) { acquired = 1; break; }
        CHECK(a->lease.slot == 0 && a->lease.generation == 0);
    }
    CHECK(acquired);
    OK(ios27_21e_cancel_register(r->tls, &a->token));
    OK(ios27_21e_setspecific(r->tls, r->key, a));
    OK(ios27_21e_cleanup_push(r->tls, cleanup, a, &scope));
    if (a->actor < PRODUCERS) produce(a);
    else consume(a);
    OK(pthread_mutex_lock(&r->gate));
    ++r->ready;
    /* Coordinator and workers share this CV: a signal can wake a worker
       instead of the coordinator, causing a missed readiness transition. */
    OK(pthread_cond_broadcast(&r->gate_changed));
    while (!r->release) OK(pthread_cond_wait(&r->gate_changed, &r->gate));
    OK(pthread_mutex_unlock(&r->gate));
    CHECK(ios27_21e_cancel_checkpoint(r->tls) == ECANCELED);
    OK(ios27_21j_record(r->diag, IOS27_21J_CHECKPOINT,
                         a->actor, ECANCELED, 0));
    OK(ios27_21e_cleanup_pop(r->tls, scope, 1));
    OK(ios27_21e_cancel_unregister(r->tls, a->token));
    OK(ios27_21e_thread_detach(r->tls));
    OK(ios27_21j_release(r->diag, a->lease));
    return a;
}
static void run_once(unsigned round) {
    run_state r = {0};
    ios27_21j_snapshot_data info;
    ios27_21c_handle recycled;
    ios27_21e_handle old_key;
    size_t active, keys, tokens, used;
    uint64_t cleanups, destructors;
    struct timespec expired;
    const unsigned total = PRODUCERS * ITEMS;
    r.seed = UINT64_C(0x21a000009f) + round;
    OK(ios27_21b_runtime_create(&r.threads, WORKERS));
    OK(ios27_21c_runtime_create(&r.sync, 4));
    OK(ios27_21e_create(&r.tls));
    OK(ios27_21j_create(&r.diag, WORKERS, r.seed, 1250));
    OK(ios27_21e_key_create(r.tls, tls_destructor, &r.key));
    old_key = r.key;
    OK(ios27_21c_sync_create(r.sync, IOS27_21C_ERRORCHECK_MUTEX, &r.mutex));
    OK(ios27_21c_sync_create(r.sync, IOS27_21C_CONDITION, &r.not_empty));
    OK(ios27_21c_sync_create(r.sync, IOS27_21C_CONDITION, &r.not_full));
    OK(pthread_mutex_init(&r.gate, NULL));
    OK(pthread_cond_init(&r.gate_changed, NULL));

    /* Guaranteed timeout with no racing signal; mutex remains reacquired. */
    lock_queue(&r, WORKERS);
    OK(clock_gettime(CLOCK_MONOTONIC, &expired) ? errno : 0);
    expired.tv_sec -= 1;
    CHECK(ios27_21c_cond_timedwait(r.sync, r.not_empty, r.mutex,
                                   IOS27_21C_MONOTONIC, &expired) == ETIMEDOUT);
    OK(ios27_21j_record(r.diag, IOS27_21J_TIMEOUT, WORKERS, ETIMEDOUT, round));
    OK(ios27_21c_mutex_unlock(r.sync, r.mutex));
    /* Consumers start first to exercise empty queue and signal ordering. */
    for (unsigned i = 0; i < WORKERS; ++i) {
        unsigned actor = (i + CONSUMERS) % WORKERS;
        r.args[actor] = (worker_args){.run = &r, .actor = actor};
        OK(ios27_21b_thread_create(r.threads, &r.ids[actor], NULL,
                                     worker, &r.args[actor]));
    }
    OK(pthread_mutex_lock(&r.gate));
    while (r.ready != WORKERS)
        OK(pthread_cond_wait(&r.gate_changed, &r.gate));
    OK(pthread_mutex_unlock(&r.gate));
    CHECK(r.produced == total && r.consumed == total && r.size == 0);
    CHECK(r.sum == (uint64_t)total * (total + 1u) / 2u);
    CHECK(ios27_21b_runtime_destroy(r.threads) == EBUSY);
    OK(ios27_21e_stats(r.tls, &active, &keys, &tokens, NULL, NULL));
    CHECK(active == WORKERS && keys == 1 && tokens == WORKERS);
    OK(ios27_21j_snapshot(r.diag, &info));
    CHECK(info.active == WORKERS && info.peak == WORKERS);
    CHECK(info.probes >= total * 2u + WORKERS &&
          info.probes <= total * 2u + WORKERS * 128u && info.injections != 0);
    /* Recompute the exact same set of keyed fault decisions on a fresh
       runtime. Thread scheduling may reorder events but cannot change this
       probe count or which item ids produce ENOMEM. */
    ios27_21j_diagnostics *replay = NULL;
    ios27_21j_snapshot_data expected;
    OK(ios27_21j_create(&replay, 1, r.seed, 1250));
    for (unsigned actor = 0; actor < WORKERS; ++actor) {
        int acquired = 0;
        for (unsigned attempt = 0; attempt < 128u; ++attempt) {
            int rc = ios27_21j_probe(replay, 13, actor, attempt);
            CHECK(rc == 0 || rc == ENOMEM);
            if (!rc) { acquired = 1; break; }
        }
        CHECK(acquired);
    }
    for (unsigned actor = 0; actor < PRODUCERS; ++actor) {
        for (unsigned i = 0; i < ITEMS; ++i) {
            int rc = ios27_21j_probe(replay, 31u + (actor & 1u), actor, i);
            CHECK(rc == 0 || rc == ENOMEM);
        }
    }
    for (unsigned value = 1; value <= total; ++value) {
        int rc = ios27_21j_probe(replay, 31u + ((WORKERS + 1u) & 1u),
                                 WORKERS + 1u, value);
        CHECK(rc == 0 || rc == ENOMEM);
    }
    OK(ios27_21j_snapshot(replay, &expected));
    CHECK(info.injections == expected.injections);
    CHECK(info.probes == expected.probes);
    OK(ios27_21j_destroy(replay));
    for (unsigned i = 0; i < WORKERS; ++i)
        OK(ios27_21e_cancel_request(r.tls, r.args[i].token));
    OK(pthread_mutex_lock(&r.gate));
    r.release = 1;
    OK(pthread_cond_broadcast(&r.gate_changed));
    OK(pthread_mutex_unlock(&r.gate));
    for (unsigned i = 0; i < WORKERS; ++i) {
        void *value = NULL;
        OK(ios27_21b_thread_join(r.threads, r.ids[i], &value));
        CHECK(value == &r.args[i]);
        CHECK(ios27_21b_thread_join(r.threads, r.ids[i], NULL) == ESRCH);
        CHECK(ios27_21e_cancel_request(r.tls, r.args[i].token) == EINVAL);
    }
    OK(ios27_21b_runtime_usage(r.threads, &used));
    CHECK(used == 0);
    OK(ios27_21e_stats(r.tls, &active, &keys, &tokens, &cleanups, &destructors));
    CHECK(active == 0 && tokens == 0 && keys == 1);
    CHECK(cleanups == WORKERS && destructors == WORKERS);
    CHECK(atomic_load(&r.cleanups) == WORKERS);
    CHECK(atomic_load(&r.destructors) == WORKERS);
    OK(ios27_21j_snapshot(r.diag, &info));
    CHECK(info.active == 0 && info.leases_created == WORKERS);
    CHECK(info.leases_released == WORKERS && info.timeouts >= 1);
    CHECK(info.contention_ns_max <= info.contention_ns_total);
    uint64_t measured = 0;
    for (unsigned b = 0; b < 5u; ++b)
        measured += info.contention_ns_buckets[b];
    CHECK(measured == info.contention);
    CHECK(info.waits > 0 && info.next_sequence > IOS27_21J_TRACE_CAPACITY);
    CHECK(info.dropped > 0);
    ios27_21c_usage(r.sync, &used);
    CHECK(used == 3);
    OK(ios27_21c_sync_destroy(r.sync, r.not_empty));
    OK(ios27_21c_sync_destroy(r.sync, r.not_full));
    OK(ios27_21c_sync_destroy(r.sync, r.mutex));
    OK(ios27_21c_sync_create(r.sync, IOS27_21C_NORMAL_MUTEX, &recycled));
    CHECK(ios27_21c_mutex_lock(r.sync, r.mutex) == EINVAL);
    OK(ios27_21c_sync_destroy(r.sync, recycled));
    OK(ios27_21e_key_delete(r.tls, old_key));
    CHECK(ios27_21e_key_delete(r.tls, old_key) == EINVAL);
    OK(ios27_21j_destroy(r.diag));
    OK(ios27_21e_destroy(r.tls));
    OK(ios27_21c_runtime_destroy(r.sync));
    OK(ios27_21b_runtime_destroy(r.threads));
    OK(pthread_cond_destroy(&r.gate_changed));
    OK(pthread_mutex_destroy(&r.gate));
    printf("phase21j round %u: %u items, %llu injected probes, %llu waits, %llu contention; PASS\n",
           round, total, (unsigned long long)info.injections,
           (unsigned long long)info.waits, (unsigned long long)info.contention);
}
int main(void) {
    for (unsigned r = 0; r < ROUNDS; ++r) run_once(r);
    puts("phase21j integration: 60k queued items, cancellation, diagnostics and repeated teardown; PASS");
    return 0;
}
