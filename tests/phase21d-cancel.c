#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21d_rw_once.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(call) do { int rc_ = (call); if (rc_) { \
    fprintf(stderr, "%s:%d: %s => %d\n", __FILE__, __LINE__, #call, rc_); abort(); \
} } while (0)
#define REQUIRE(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #x); abort(); \
} } while (0)

typedef struct {
    ios27_21d_runtime *rt;
    ios27_21d_handle lock;
    atomic_int entered;
    atomic_int unlocked;
} lock_data;
static void *cancelled_reader(void *opaque) {
    lock_data *s = opaque;
    CHECK(ios27_21d_rdlock(s->rt, s->lock));
    atomic_store(&s->entered, 1);
    CHECK(ios27_21d_unlock(s->rt, s->lock));
    atomic_store(&s->unlocked, 1);
    /* The cancellation requested while waiting must only be delivered
       after the reader has completely released its ownership. */
    pthread_testcancel();
    return NULL;
}
static void wait_queued(ios27_21d_runtime *rt, size_t target) {
    size_t count = 0;
    for (unsigned i = 0; i < 2500000u; ++i) {
        CHECK(ios27_21d_usage(rt, NULL, &count, NULL));
        if (count >= target) return;
        (void)sched_yield();
    }
    REQUIRE(count >= target);
}
static void test_cancelled_reader(void) {
    lock_data s = {0};
    pthread_t worker;
    void *out = NULL;
    CHECK(ios27_21d_runtime_create(&s.rt, 1));
    CHECK(ios27_21d_create(s.rt, IOS27_21D_RWLOCK, &s.lock));
    CHECK(ios27_21d_wrlock(s.rt, s.lock));
    CHECK(pthread_create(&worker, NULL, cancelled_reader, &s));
    wait_queued(s.rt, 1);
    CHECK(pthread_cancel(worker));
    REQUIRE(ios27_21d_destroy(s.rt, s.lock) == EBUSY);
    CHECK(ios27_21d_unlock(s.rt, s.lock));
    CHECK(pthread_join(worker, &out));
    REQUIRE(out == PTHREAD_CANCELED);
    REQUIRE(atomic_load(&s.entered) && atomic_load(&s.unlocked));
    CHECK(ios27_21d_destroy(s.rt, s.lock));
    CHECK(ios27_21d_runtime_destroy(s.rt));
}

typedef struct {
    ios27_21d_runtime *rt;
    ios27_21d_handle once;
    pthread_barrier_t entered;
    pthread_barrier_t resumed;
    atomic_int committed;
    atomic_int returned;
} once_data;
static int initialize(void *opaque) {
    once_data *s = opaque;
    int rc = pthread_barrier_wait(&s->entered);
    REQUIRE(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    rc = pthread_barrier_wait(&s->resumed);
    REQUIRE(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    atomic_store(&s->committed, 1);
    return 0;
}
static void *cancelled_initializer(void *opaque) {
    once_data *s = opaque;
    CHECK(ios27_21d_once(s->rt, s->once, initialize, s));
    atomic_store(&s->returned, 1);
    pthread_testcancel();
    return NULL;
}
static int unexpected_initializer(void *opaque) {
    (void)opaque;
    REQUIRE(0 && "once initializer unexpectedly reran");
    return EINVAL;
}
static void test_cancelled_once(void) {
    once_data s = {0};
    pthread_t worker;
    void *out = NULL;
    CHECK(ios27_21d_runtime_create(&s.rt, 1));
    CHECK(ios27_21d_create(s.rt, IOS27_21D_ONCE, &s.once));
    CHECK(pthread_barrier_init(&s.entered, NULL, 2));
    CHECK(pthread_barrier_init(&s.resumed, NULL, 2));
    CHECK(pthread_create(&worker, NULL, cancelled_initializer, &s));
    int rc = pthread_barrier_wait(&s.entered);
    REQUIRE(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    REQUIRE(ios27_21d_destroy(s.rt, s.once) == EBUSY);
    CHECK(pthread_cancel(worker));
    rc = pthread_barrier_wait(&s.resumed);
    REQUIRE(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    CHECK(pthread_join(worker, &out));
    REQUIRE(out == PTHREAD_CANCELED);
    REQUIRE(atomic_load(&s.committed) && atomic_load(&s.returned));
    CHECK(ios27_21d_once(s.rt, s.once, unexpected_initializer, NULL));
    CHECK(pthread_barrier_destroy(&s.entered));
    CHECK(pthread_barrier_destroy(&s.resumed));
    CHECK(ios27_21d_destroy(s.rt, s.once));
    CHECK(ios27_21d_runtime_destroy(s.rt));
}

static int fail_initialize(void *opaque) {
    once_data *s = opaque;
    int rc = pthread_barrier_wait(&s->entered);
    REQUIRE(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    rc = pthread_barrier_wait(&s->resumed);
    REQUIRE(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    atomic_store(&s->committed, 1);
    return EAGAIN;
}
static int retry_initialize(void *opaque) {
    once_data *s = opaque;
    atomic_store(&s->committed, 2);
    return 0;
}
static void *cancelled_failed_initializer(void *opaque) {
    once_data *s = opaque;
    REQUIRE(ios27_21d_once(s->rt, s->once, fail_initialize, s) == EAGAIN);
    atomic_store(&s->returned, 1);
    pthread_testcancel();
    return NULL;
}
static void test_cancelled_rollback(void) {
    once_data s = {0};
    pthread_t worker;
    void *out = NULL;
    CHECK(ios27_21d_runtime_create(&s.rt, 1));
    CHECK(ios27_21d_create(s.rt, IOS27_21D_ONCE, &s.once));
    CHECK(pthread_barrier_init(&s.entered, NULL, 2));
    CHECK(pthread_barrier_init(&s.resumed, NULL, 2));
    CHECK(pthread_create(&worker, NULL, cancelled_failed_initializer, &s));
    int rc = pthread_barrier_wait(&s.entered);
    REQUIRE(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    CHECK(pthread_cancel(worker));
    rc = pthread_barrier_wait(&s.resumed);
    REQUIRE(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    CHECK(pthread_join(worker, &out));
    REQUIRE(out == PTHREAD_CANCELED);
    REQUIRE(atomic_load(&s.committed) == 1 && atomic_load(&s.returned));
    CHECK(ios27_21d_once(s.rt, s.once, retry_initialize, &s));
    REQUIRE(atomic_load(&s.committed) == 2);
    CHECK(pthread_barrier_destroy(&s.entered));
    CHECK(pthread_barrier_destroy(&s.resumed));
    CHECK(ios27_21d_destroy(s.rt, s.once));
    CHECK(ios27_21d_runtime_destroy(s.rt));
}

int main(void) {
    test_cancelled_reader();
    test_cancelled_once();
    test_cancelled_rollback();
    puts("phase21d: deferred cancellation cannot orphan queued locks or once initialization");
    return 0;
}
