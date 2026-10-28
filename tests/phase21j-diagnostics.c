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
    fprintf(stderr, "phase21j diagnostics:%d: %s\n", __LINE__, #x); abort(); \
} } while (0)

enum { ACTORS = 8, LOOPS = 3000, RACES = 256 };
static void faults_and_overflow(void) {
    ios27_21j_diagnostics *a = NULL, *b = NULL;
    ios27_21j_lease first, second, newer;
    ios27_21j_snapshot_data info;
    ios27_21j_event event;
    unsigned injected = 0;
    CHECK(ios27_21j_create(NULL, 2, 17, 100) == EINVAL);
    CHECK(ios27_21j_create(&a, 0, 17, 100) == EINVAL);
    CHECK(ios27_21j_create(&a, IOS27_21J_MAX_LEASES + 1u, 17, 100) == EINVAL);
    CHECK(ios27_21j_create(&a, 2, 17, 10001) == EINVAL);
    CHECK(ios27_21j_create(&a, 2, UINT64_C(0x21feed), 2500) == 0);
    CHECK(ios27_21j_create(&b, 2, UINT64_C(0x21feed), 2500) == 0);
    for (uint64_t i = 0; i < 20000; ++i) {
        int ra = ios27_21j_probe(a, 3u + (uint32_t)(i % 5), (uint32_t)(i % 11), i);
        int rb = ios27_21j_probe(b, 3u + (uint32_t)(i % 5), (uint32_t)(i % 11), i);
        CHECK((ra == 0 || ra == ENOMEM) && ra == rb);
        injected += (unsigned)(ra == ENOMEM);
    }
    CHECK(injected > 3000 && injected < 7000);
    CHECK(ios27_21j_probe(a, 0, 1, 0) == EINVAL);
    CHECK(ios27_21j_acquire(a, &first) == 0);
    CHECK(ios27_21j_acquire(a, &second) == 0);
    CHECK(ios27_21j_destroy(a) == EBUSY);
    CHECK(ios27_21j_acquire(a, &newer) == EAGAIN);
    CHECK(ios27_21j_release(b, first) == EINVAL);
    CHECK(ios27_21j_release(a, first) == 0);
    CHECK(ios27_21j_release(a, first) == EINVAL);
    CHECK(ios27_21j_acquire(a, &newer) == 0);
    CHECK(newer.slot == first.slot && newer.generation != first.generation);
    CHECK(ios27_21j_release(a, first) == EINVAL);
    CHECK(ios27_21j_release(a, newer) == 0);
    CHECK(ios27_21j_release(a, second) == 0);
    CHECK(ios27_21j_snapshot(a, &info) == 0);
    CHECK(info.active == 0 && info.peak == 2 && info.capacity == 2);
    CHECK(info.probes == 20000 && info.injections == injected);
    CHECK(info.leases_created == info.leases_released);
    CHECK(info.dropped > 0 && info.next_sequence > IOS27_21J_TRACE_CAPACITY);
    CHECK(ios27_21j_read(a, 1, &event) == ERANGE);
    CHECK(ios27_21j_read(a, info.next_sequence + 1, &event) == ERANGE);
    CHECK(ios27_21j_read(a, 0, &event) == EINVAL);
    CHECK(ios27_21j_read(a, info.next_sequence, &event) == 0);
    CHECK(event.sequence == info.next_sequence && event.kind == IOS27_21J_RELEASE);
    CHECK(ios27_21j_destroy(a) == 0);
    CHECK(ios27_21j_destroy(b) == 0);
}
typedef struct { ios27_21j_diagnostics *d; unsigned actor; } churn_arg;
static _Atomic unsigned created;
static void *churn(void *arg) {
    static const uint64_t levels[5] = {1, 5000, 50000, 500000, 5000000};
    churn_arg *a = arg;
    for (unsigned i = 0; i < LOOPS; ++i) {
        ios27_21j_lease handle;
        int rc = EAGAIN;
        for (unsigned tries = 0; tries < 200000u && rc == EAGAIN; ++tries) {
            rc = ios27_21j_acquire(a->d, &handle);
            if (rc == EAGAIN) sched_yield();
        }
        CHECK(rc == 0);
        CHECK(ios27_21j_probe(a->d, 11, a->actor, i) == 0);
        CHECK(ios27_21j_note_contention(a->d, a->actor,
                                         levels[i % 5u]) == 0);
        CHECK(ios27_21j_release(a->d, handle) == 0);
        CHECK(ios27_21j_release(a->d, handle) == EINVAL);
        atomic_fetch_add(&created, 1u);
    }
    return NULL;
}
typedef struct {
    ios27_21j_diagnostics *d;
    ios27_21j_lease handle;
    pthread_barrier_t *start;
    int result;
} loser;
static void *release_racer(void *value) {
    loser *r = value;
    int rc = pthread_barrier_wait(r->start);
    CHECK(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    r->result = ios27_21j_release(r->d, r->handle);
    return NULL;
}
static void races_and_quota(void) {
    ios27_21j_diagnostics *d = NULL;
    ios27_21j_snapshot_data info;
    pthread_t threads[ACTORS];
    churn_arg args[ACTORS];
    CHECK(ios27_21j_create(&d, 4, 99, 0) == 0);
    for (unsigned i = 0; i < ACTORS; ++i) {
        args[i] = (churn_arg){d, i};
        CHECK(pthread_create(&threads[i], NULL, churn, &args[i]) == 0);
    }
    for (unsigned i = 0; i < ACTORS; ++i)
        CHECK(pthread_join(threads[i], NULL) == 0);
    CHECK(atomic_load(&created) == ACTORS * LOOPS);
    CHECK(ios27_21j_snapshot(d, &info) == 0);
    CHECK(info.active == 0 && info.peak <= 4);
    CHECK(info.leases_created == ACTORS * LOOPS);
    CHECK(info.leases_created == info.leases_released);
    CHECK(info.contention == ACTORS * LOOPS);
    CHECK(info.contention_ns_max == 5000000u);
    CHECK(info.contention_ns_total == UINT64_C(5555001) * (ACTORS * LOOPS / 5u));
    for (unsigned bucket = 0; bucket < 5; ++bucket)
        CHECK(info.contention_ns_buckets[bucket] == ACTORS * LOOPS / 5u);
    for (unsigned i = 0; i < RACES; ++i) {
        ios27_21j_lease h;
        pthread_barrier_t barrier;
        pthread_t x, y;
        loser a, b;
        CHECK(ios27_21j_acquire(d, &h) == 0);
        CHECK(pthread_barrier_init(&barrier, NULL, 3) == 0);
        a = (loser){d, h, &barrier, -1};
        b = (loser){d, h, &barrier, -1};
        CHECK(pthread_create(&x, NULL, release_racer, &a) == 0);
        CHECK(pthread_create(&y, NULL, release_racer, &b) == 0);
        int rc = pthread_barrier_wait(&barrier);
        CHECK(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
        CHECK(pthread_join(x, NULL) == 0);
        CHECK(pthread_join(y, NULL) == 0);
        CHECK((a.result == 0 && b.result == EINVAL) ||
              (a.result == EINVAL && b.result == 0));
        CHECK(pthread_barrier_destroy(&barrier) == 0);
    }
    CHECK(ios27_21j_snapshot(d, &info) == 0);
    CHECK(info.active == 0 && info.leases_created == info.leases_released);
    CHECK(info.leases_created == ACTORS * LOOPS + RACES);
    CHECK(ios27_21j_destroy(d) == 0);
}
static void faulted_acquisition_replay(void) {
    ios27_21j_diagnostics *a = NULL, *b = NULL;
    ios27_21j_snapshot_data sa, sb;
    unsigned refused = 0, accepted = 0;
    CHECK(ios27_21j_create(&a, 1, 7001, 3500) == 0);
    CHECK(ios27_21j_create(&b, 1, 7001, 3500) == 0);
    for (uint64_t i = 0; i < 10000; ++i) {
        ios27_21j_lease x = {0}, y = {0};
        int ra = ios27_21j_acquire_injected(a, 77, (uint32_t)(i % 8), i, &x);
        int rb = ios27_21j_acquire_injected(b, 77, (uint32_t)(i % 8), i, &y);
        CHECK((ra == 0 || ra == ENOMEM) && ra == rb);
        if (ra == ENOMEM) {
            CHECK(!x.slot && !x.generation && !x.owner);
            CHECK(!y.slot && !y.generation && !y.owner);
            ++refused;
        } else {
            CHECK(x.slot && y.slot && x.generation == y.generation);
            CHECK(ios27_21j_release(a, x) == 0);
            CHECK(ios27_21j_release(b, y) == 0);
            ++accepted;
        }
    }
    CHECK(refused > 2500 && refused < 4500);
    CHECK(accepted + refused == 10000);
    CHECK(ios27_21j_snapshot(a, &sa) == 0);
    CHECK(ios27_21j_snapshot(b, &sb) == 0);
    CHECK(sa.active == 0 && sb.active == 0);
    CHECK(sa.leases_created == accepted && sb.leases_created == accepted);
    CHECK(sa.leases_released == accepted && sb.leases_released == accepted);
    CHECK(sa.injections == refused && sb.injections == refused);
    CHECK(ios27_21j_destroy(a) == 0);
    CHECK(ios27_21j_destroy(b) == 0);
}
int main(void) {
    faults_and_overflow();
    faulted_acquisition_replay();
    races_and_quota();
    puts("phase21j diagnostics: 30k deterministic fault probes, 24k lease cycles, 256 release races; PASS");
    return 0;
}
