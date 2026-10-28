#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem20d_threads.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CHECK(x) do { int rc_ = (x); if (rc_) { \
    fprintf(stderr, "%s:%d: %s -> %d\n", __FILE__, __LINE__, #x, rc_); exit(1); \
} } while (0)
#define REQUIRE(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: assertion %s failed\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)
#define NWORK 8
#define LOOPS 600
static ios27_20d_runtime *rt;
static ios27_20d_handle mu, rw, cv, once, key;
static int counter, rw_counter, run_once;
static atomic_int tls_cleanup;
static atomic_int detached_done;
static atomic_int gate_ready;
static atomic_int gate_open;

typedef struct { int id; atomic_int *destroyed; } data_item;
static void tls_destruct(void *value) {
    data_item *p = value;
    atomic_fetch_add_explicit(p->destroyed, 1, memory_order_relaxed);
    free(p);
}
static void initialize(void *context) {
    int *value = context;
    ++*value;
}
static void *worker(void *context) {
    int id = *(int *)context;
    data_item *p = malloc(sizeof(*p));
    void *read_back = NULL;
    REQUIRE(p != NULL);
    p->id = id;
    p->destroyed = &tls_cleanup;
    CHECK(ios27_20d_setspecific(rt, key, p));
    CHECK(ios27_20d_getspecific(rt, key, &read_back));
    REQUIRE(read_back == p);
    REQUIRE(ios27_20d_thread_self(rt).slot != 0u);
    for (int i = 0; i < LOOPS; ++i) {
        CHECK(ios27_20d_once(rt, once, initialize, &run_once));
        CHECK(ios27_20d_mutex_lock(rt, mu));
        ++counter;
        CHECK(ios27_20d_mutex_unlock(rt, mu));
        CHECK(ios27_20d_rwlock_wrlock(rt, rw));
        ++rw_counter;
        CHECK(ios27_20d_rwlock_unlock(rt, rw));
        CHECK(ios27_20d_rwlock_rdlock(rt, rw));
        REQUIRE(rw_counter >= 0);
        CHECK(ios27_20d_rwlock_unlock(rt, rw));
        REQUIRE(ios27_20d_cancel_point(rt) == 0);
    }
    return (void *)(uintptr_t)(id + 1);
}
static int64_t realtime_plus_ms(long ms) {
    struct timespec ts;
    CHECK(clock_gettime(CLOCK_REALTIME, &ts));
    return (int64_t)ts.tv_sec * INT64_C(1000000000) + ts.tv_nsec +
           (int64_t)ms * INT64_C(1000000);
}
static void *held_worker(void *ignored) {
    (void)ignored;
    atomic_fetch_add(&gate_ready, 1);
    while (!atomic_load(&gate_open)) {
        struct timespec ts = {0, 1000000L};
        (void)nanosleep(&ts, NULL);
    }
    return NULL;
}
static void *cancel_worker(void *ignored) {
    (void)ignored;
    atomic_fetch_add(&gate_ready, 1);
    while (!atomic_load(&gate_open)) {
        struct timespec ts = {0, 1000000L};
        (void)nanosleep(&ts, NULL);
    }
    return (void *)(uintptr_t)(unsigned)ios27_20d_cancel_point(rt);
}
static void *detached_worker(void *ignored) {
    (void)ignored;
    atomic_store(&detached_done, 1);
    return NULL;
}
static void wait_for_atomic(atomic_int *flag, int expected) {
    struct timespec ts = {0, 1000000L};
    for (int i = 0; i < 5000 && atomic_load(flag) != expected; ++i)
        (void)nanosleep(&ts, NULL);
    REQUIRE(atomic_load(flag) == expected);
}
static void test_sync_workload(void) {
    ios27_20d_handle workers[NWORK], old;
    int args[NWORK];
    size_t t, s, k;
    CHECK(ios27_20d_create(&rt, NWORK));
    CHECK(ios27_20d_sync_create(rt, IOS27_20D_MUTEX, &mu));
    CHECK(ios27_20d_sync_create(rt, IOS27_20D_RWLOCK, &rw));
    CHECK(ios27_20d_sync_create(rt, IOS27_20D_CONDITION, &cv));
    CHECK(ios27_20d_sync_create(rt, IOS27_20D_ONCE, &once));
    CHECK(ios27_20d_key_create(rt, &key, tls_destruct));
    REQUIRE(ios27_20d_mutex_unlock(rt, mu) == EPERM);
    CHECK(ios27_20d_mutex_lock(rt, mu));
    REQUIRE(ios27_20d_mutex_trylock(rt, mu) == EBUSY);
    REQUIRE(ios27_20d_sync_destroy(rt, mu) == EBUSY);
    REQUIRE(ios27_20d_cond_wait(rt, cv, mu, realtime_plus_ms(5)) == ETIMEDOUT);
    CHECK(ios27_20d_mutex_unlock(rt, mu));
    CHECK(ios27_20d_cond_signal(rt, cv, 1));
    for (int i = 0; i < NWORK; ++i) {
        args[i] = i;
        CHECK(ios27_20d_thread_create(rt, &workers[i], worker, &args[i]));
    }
    REQUIRE(ios27_20d_thread_create(rt, &old, worker, &args[0]) == EAGAIN);
    REQUIRE(ios27_20d_destroy(rt) == EBUSY);
    old = workers[0];
    for (int i = 0; i < NWORK; ++i) {
        void *result = NULL;
        CHECK(ios27_20d_thread_join(rt, workers[i], &result));
        REQUIRE((uintptr_t)result == (uintptr_t)(i + 1));
    }
    REQUIRE(ios27_20d_thread_join(rt, old, NULL) == ESRCH);
    REQUIRE(counter == NWORK * LOOPS);
    REQUIRE(rw_counter == counter);
    REQUIRE(run_once == 1);
    REQUIRE(atomic_load(&tls_cleanup) == NWORK);
    ios27_20d_usage(rt, &t, &s, &k);
    REQUIRE(t == 0 && s == 4 && k == 1);
    CHECK(ios27_20d_key_delete(rt, key));
    void *check_stale = NULL;
    REQUIRE(ios27_20d_getspecific(rt, key, &check_stale) == EINVAL);
    CHECK(ios27_20d_sync_destroy(rt, once));
    CHECK(ios27_20d_sync_destroy(rt, cv));
    CHECK(ios27_20d_sync_destroy(rt, rw));
    CHECK(ios27_20d_sync_destroy(rt, mu));
    REQUIRE(ios27_20d_mutex_lock(rt, mu) == EINVAL);
    CHECK(ios27_20d_destroy(rt));
}
static void test_recursive_and_exhaustion(void) {
    ios27_20d_handle rec, thread[2], third;
    CHECK(ios27_20d_create(&rt, 2));
    CHECK(ios27_20d_sync_create(rt, IOS27_20D_RECURSIVE_MUTEX, &rec));
    CHECK(ios27_20d_mutex_lock(rt, rec));
    CHECK(ios27_20d_mutex_lock(rt, rec));
    REQUIRE(ios27_20d_sync_destroy(rt, rec) == EBUSY);
    CHECK(ios27_20d_mutex_unlock(rt, rec));
    CHECK(ios27_20d_mutex_unlock(rt, rec));
    atomic_store(&gate_open, 0);
    atomic_store(&gate_ready, 0);
    CHECK(ios27_20d_thread_create(rt, &thread[0], held_worker, NULL));
    CHECK(ios27_20d_thread_create(rt, &thread[1], held_worker, NULL));
    wait_for_atomic(&gate_ready, 2);
    REQUIRE(ios27_20d_thread_create(rt, &third, held_worker, NULL) == EAGAIN);
    atomic_store(&gate_open, 1);
    CHECK(ios27_20d_thread_join(rt, thread[0], NULL));
    CHECK(ios27_20d_thread_join(rt, thread[1], NULL));
    REQUIRE(ios27_20d_thread_join(rt, thread[1], NULL) == ESRCH);
    CHECK(ios27_20d_sync_destroy(rt, rec));
    CHECK(ios27_20d_destroy(rt));
}
static void test_cancel_and_detach(void) {
    ios27_20d_handle thread;
    void *value;
    CHECK(ios27_20d_create(&rt, 1));
    atomic_store(&gate_open, 0);
    atomic_store(&gate_ready, 0);
    CHECK(ios27_20d_thread_create(rt, &thread, cancel_worker, NULL));
    wait_for_atomic(&gate_ready, 1);
    CHECK(ios27_20d_cancel_request(rt, thread));
    atomic_store(&gate_open, 1);
    CHECK(ios27_20d_thread_join(rt, thread, &value));
    REQUIRE((uintptr_t)value == (uintptr_t)ECANCELED);
    REQUIRE(ios27_20d_cancel_request(rt, thread) == ESRCH);
    atomic_store(&detached_done, 0);
    CHECK(ios27_20d_thread_create(rt, &thread, detached_worker, NULL));
    CHECK(ios27_20d_thread_detach(rt, thread));
    wait_for_atomic(&detached_done, 1);
    for (int i = 0; i < 5000; ++i) {
        size_t n;
        struct timespec ts = {0, 1000000L};
        ios27_20d_usage(rt, &n, NULL, NULL);
        if (n == 0 && ios27_20d_destroy(rt) == 0) return;
        (void)nanosleep(&ts, NULL);
    }
    REQUIRE(0 && "detached worker leaked");
}
int main(void) {
    test_sync_workload();
    test_recursive_and_exhaustion();
    test_cancel_and_detach();
    puts("phase20d: native integration, contention, quotas, tls and teardown passed");
    return 0;
}
