#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21d_rw_once.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(call) do { int x_ = (call); if (x_) { \
    fprintf(stderr, "%s:%d: %s -> %d\n", __FILE__, __LINE__, #call, x_); abort(); \
} } while (0)
#define REQUIRE(test) do { if (!(test)) { \
    fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #test); abort(); \
} } while (0)
#define READERS 12

typedef struct {
    ios27_21d_runtime *rt;
    ios27_21d_handle lock;
    pthread_barrier_t inside;
    pthread_barrier_t release;
    atomic_int concurrent;
    atomic_int max_concurrent;
} state;

static void *reader(void *ctx) {
    state *s = ctx;
    CHECK(ios27_21d_rdlock(s->rt, s->lock));
    int n = atomic_fetch_add(&s->concurrent, 1) + 1;
    int old = atomic_load(&s->max_concurrent);
    while (n > old && !atomic_compare_exchange_weak(&s->max_concurrent, &old, n)) { }
    int rc = pthread_barrier_wait(&s->inside);
    REQUIRE(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    rc = pthread_barrier_wait(&s->release);
    REQUIRE(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    atomic_fetch_sub(&s->concurrent, 1);
    CHECK(ios27_21d_unlock(s->rt, s->lock));
    return NULL;
}

static void *invalid_unlock(void *ctx) {
    state *s = ctx;
    REQUIRE(ios27_21d_unlock(s->rt, s->lock) == EPERM);
    REQUIRE(ios27_21d_tryrdlock(s->rt, s->lock) == EBUSY);
    return NULL;
}


static void *foreign_reader_unlock(void *ctx) {
    state *s = ctx;
    REQUIRE(ios27_21d_unlock(s->rt, s->lock) == EPERM);
    return NULL;
}

/* Verify that consecutive queued readers are admitted as a single cohort
   before a later queued writer; neither reader can finish alone. */
typedef struct {
    ios27_21d_runtime *rt;
    ios27_21d_handle lock;
    pthread_barrier_t cohort;
    atomic_int cursor;
    int order[3];
    int result[3];
} fifo_state;
typedef struct { fifo_state *state; int id; } fifo_arg;
static void *queued_worker(void *opaque) {
    fifo_arg *arg = opaque;
    fifo_state *s = arg->state;
    int err = arg->id < 2 ? ios27_21d_rdlock(s->rt, s->lock) :
                            ios27_21d_wrlock(s->rt, s->lock);
    s->result[arg->id] = err;
    if (err) return NULL;
    int index = atomic_fetch_add(&s->cursor, 1);
    REQUIRE(index >= 0 && index < 3);
    s->order[index] = arg->id;
    if (arg->id < 2) {
        int rc = pthread_barrier_wait(&s->cohort);
        REQUIRE(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    }
    CHECK(ios27_21d_unlock(s->rt, s->lock));
    return NULL;
}
static void wait_for_queue(ios27_21d_runtime *rt, size_t expected) {
    size_t queued = 0;
    for (unsigned i = 0; i < 2500000u; ++i) {
        CHECK(ios27_21d_usage(rt, NULL, &queued, NULL));
        if (queued >= expected) return;
        (void)sched_yield();
    }
    REQUIRE(queued >= expected);
}
static void test_batched_reader_order(void) {
    fifo_state s = {0};
    pthread_t threads[3];
    fifo_arg args[3];
    CHECK(ios27_21d_runtime_create(&s.rt, 1));
    CHECK(ios27_21d_create(s.rt, IOS27_21D_RWLOCK, &s.lock));
    CHECK(pthread_barrier_init(&s.cohort, NULL, 2));
    CHECK(ios27_21d_wrlock(s.rt, s.lock));
    for (int i = 0; i < 3; ++i) {
        args[i] = (fifo_arg){&s, i};
        CHECK(pthread_create(&threads[i], NULL, queued_worker, &args[i]));
        wait_for_queue(s.rt, (size_t)i + 1u);
    }
    CHECK(ios27_21d_unlock(s.rt, s.lock));
    for (int i = 0; i < 3; ++i) CHECK(pthread_join(threads[i], NULL));
    for (int i = 0; i < 3; ++i) REQUIRE(s.result[i] == 0);
    REQUIRE((s.order[0] == 0 || s.order[0] == 1));
    REQUIRE((s.order[1] == 0 || s.order[1] == 1));
    REQUIRE(s.order[0] != s.order[1]);
    REQUIRE(s.order[2] == 2);
    CHECK(pthread_barrier_destroy(&s.cohort));
    CHECK(ios27_21d_destroy(s.rt, s.lock));
    CHECK(ios27_21d_runtime_destroy(s.rt));
    puts("phase21d: fifo reader cohorts overlap and precede later writer");
}


static void test_runtime_scope(void) {
    ios27_21d_runtime *a, *b;
    ios27_21d_handle ha, hb;
    CHECK(ios27_21d_runtime_create(&a, 1));
    CHECK(ios27_21d_runtime_create(&b, 1));
    CHECK(ios27_21d_create(a, IOS27_21D_RWLOCK, &ha));
    CHECK(ios27_21d_create(b, IOS27_21D_RWLOCK, &hb));
    REQUIRE(ha.slot == hb.slot && ha.generation == hb.generation);
    REQUIRE(ha.runtime_cookie != hb.runtime_cookie);
    REQUIRE(ios27_21d_wrlock(b, ha) == EINVAL);
    REQUIRE(ios27_21d_tryrdlock(a, hb) == EINVAL);
    REQUIRE(ios27_21d_destroy(a, hb) == EINVAL);
    CHECK(ios27_21d_rdlock(a, ha));
    CHECK(ios27_21d_wrlock(b, hb));
    CHECK(ios27_21d_unlock(a, ha));
    CHECK(ios27_21d_unlock(b, hb));
    CHECK(ios27_21d_destroy(a, ha));
    CHECK(ios27_21d_destroy(b, hb));
    CHECK(ios27_21d_runtime_destroy(a));
    CHECK(ios27_21d_runtime_destroy(b));
    puts("phase21d: cross-runtime handle alias rejected despite matching slot and generation");
}

int main(void) {
    test_runtime_scope();
    test_batched_reader_order();
    state s = {0};
    pthread_t threads[READERS], outsider;
    ios27_21d_handle once;
    CHECK(ios27_21d_runtime_create(&s.rt, 2));
    CHECK(ios27_21d_create(s.rt, IOS27_21D_RWLOCK, &s.lock));
    CHECK(ios27_21d_create(s.rt, IOS27_21D_ONCE, &once));
    REQUIRE(ios27_21d_rdlock(s.rt, once) == EINVAL);
    REQUIRE(ios27_21d_once(s.rt, s.lock, NULL, NULL) == EINVAL);
    CHECK(ios27_21d_wrlock(s.rt, s.lock));
    REQUIRE(ios27_21d_rdlock(s.rt, s.lock) == EDEADLK);
    REQUIRE(ios27_21d_trywrlock(s.rt, s.lock) == EDEADLK);
    CHECK(pthread_create(&outsider, NULL, invalid_unlock, &s));
    CHECK(pthread_join(outsider, NULL));
    CHECK(ios27_21d_unlock(s.rt, s.lock));
    CHECK(ios27_21d_rdlock(s.rt, s.lock));
    CHECK(pthread_create(&outsider, NULL, foreign_reader_unlock, &s));
    CHECK(pthread_join(outsider, NULL));
    CHECK(ios27_21d_unlock(s.rt, s.lock));
    CHECK(pthread_barrier_init(&s.inside, NULL, READERS + 1));
    CHECK(pthread_barrier_init(&s.release, NULL, READERS + 1));
    for (int i = 0; i < READERS; ++i)
        CHECK(pthread_create(&threads[i], NULL, reader, &s));
    int rc = pthread_barrier_wait(&s.inside);
    REQUIRE(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    REQUIRE(atomic_load(&s.max_concurrent) == READERS);
    REQUIRE(ios27_21d_trywrlock(s.rt, s.lock) == EBUSY);
    REQUIRE(ios27_21d_destroy(s.rt, s.lock) == EBUSY);
    rc = pthread_barrier_wait(&s.release);
    REQUIRE(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    for (int i = 0; i < READERS; ++i) CHECK(pthread_join(threads[i], NULL));
    CHECK(pthread_barrier_destroy(&s.inside));
    CHECK(pthread_barrier_destroy(&s.release));
    CHECK(ios27_21d_destroy(s.rt, s.lock));
    CHECK(ios27_21d_destroy(s.rt, once));
    CHECK(ios27_21d_runtime_destroy(s.rt));
    puts("phase21d: 12 simultaneous readers, cross-thread ownership and wrong-kind paths passed");
    return 0;
}
