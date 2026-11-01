#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21d_rw_once.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CHECK(call) do { int r_ = (call); if (r_) { \
    fprintf(stderr, "%s:%d: %s => %d\n", __FILE__, __LINE__, #call, r_); abort(); \
} } while (0)
#define REQUIRE(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: failed %s\n", __FILE__, __LINE__, #x); abort(); \
} } while (0)

/* Link-time fault injection is Linux-only and tests the bridge's cleanup
   paths without modifying production source or inserting test-only hooks. */
extern void *__real_malloc(size_t);
extern int __real_pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
extern int __real_pthread_cond_destroy(pthread_cond_t *);
extern int __real_pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
extern int __real_pthread_cond_timedwait(pthread_cond_t *, pthread_mutex_t *,
                                        const struct timespec *);

static atomic_int fail_init, fail_destroy, fail_wait, fail_timed, fail_malloc,
                  waiting_for_signal;
void *__wrap_malloc(size_t amount) {
    if (atomic_exchange(&fail_malloc, 0)) return NULL;
    return __real_malloc(amount);
}
int __wrap_pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
    if (atomic_exchange(&fail_init, 0)) return EAGAIN;
    return __real_pthread_cond_init(cond, attr);
}
int __wrap_pthread_cond_destroy(pthread_cond_t *cond) {
    if (atomic_exchange(&fail_destroy, 0)) return EBUSY;
    return __real_pthread_cond_destroy(cond);
}
int __wrap_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *guard) {
    int fail = atomic_exchange(&fail_wait, 0);
    if (fail == 1) return EINVAL;
    if (fail == 3) return 0; /* legal spurious wakeup */
    if (fail == 2) {
        atomic_store(&waiting_for_signal, 1);
        int rc = __real_pthread_cond_wait(cond, guard);
        return rc ? rc : EINVAL;
    }
    return __real_pthread_cond_wait(cond, guard);
}
int __wrap_pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *guard,
                                  const struct timespec *until) {
    int fail = atomic_exchange(&fail_timed, 0);
    if (fail == 1) return EINVAL;
    if (fail == 3) return 0; /* legal spurious wakeup */
    if (fail == 2 || fail == 4) {
        atomic_store(&waiting_for_signal, 1);
        int rc = __real_pthread_cond_timedwait(cond, guard, until);
        return rc ? rc : (fail == 4 ? ETIMEDOUT : EINVAL);
    }
    return __real_pthread_cond_timedwait(cond, guard, until);
}

typedef struct {
    ios27_21d_runtime *rt;
    ios27_21d_handle object;
    int result;
} call_data;
static void *read_waiter(void *opaque) {
    call_data *call = opaque;
    call->result = ios27_21d_rdlock(call->rt, call->object);
    if (!call->result) CHECK(ios27_21d_unlock(call->rt, call->object));
    return NULL;
}
static void *write_waiter(void *opaque) {
    call_data *call = opaque;
    call->result = ios27_21d_wrlock(call->rt, call->object);
    if (!call->result) CHECK(ios27_21d_unlock(call->rt, call->object));
    return NULL;
}
static void *timed_write_waiter(void *opaque);
static int64_t deadline(void) {
    struct timespec ts;
    CHECK(clock_gettime(CLOCK_REALTIME, &ts));
    return (int64_t)ts.tv_sec * INT64_C(1000000000) + ts.tv_nsec +
           INT64_C(2000000000);
}
static void *timed_waiter(void *opaque) {
    call_data *call = opaque;
    call->result = ios27_21d_timedrdlock(call->rt, call->object, deadline());
    if (!call->result) CHECK(ios27_21d_unlock(call->rt, call->object));
    return NULL;
}
static void *timed_write_waiter(void *opaque) {
    call_data *call = opaque;
    call->result = ios27_21d_timedwrlock(call->rt, call->object, deadline());
    if (!call->result) CHECK(ios27_21d_unlock(call->rt, call->object));
    return NULL;
}
static void no_waiters(ios27_21d_runtime *rt, size_t holders) {
    size_t waiting = 9, held = 9;
    CHECK(ios27_21d_usage(rt, NULL, &waiting, &held));
    REQUIRE(waiting == 0 && held == holders);
}
static void wait_for_queue(ios27_21d_runtime *rt) {
    size_t queued = 0;
    for (unsigned i = 0; i < 3000000u; ++i) {
        CHECK(ios27_21d_usage(rt, NULL, &queued, NULL));
        if (queued) return;
        (void)sched_yield();
    }
    REQUIRE(queued == 1);
}
static void test_rw_faults(void) {
    ios27_21d_runtime *rt;
    ios27_21d_handle lock;
    pthread_t thread;
    call_data job;
    CHECK(ios27_21d_runtime_create(&rt, 1));
    CHECK(ios27_21d_create(rt, IOS27_21D_RWLOCK, &lock));
    job = (call_data){rt, lock, -1};
    CHECK(ios27_21d_wrlock(rt, lock));
    atomic_store(&fail_malloc, 1);
    CHECK(pthread_create(&thread, NULL, read_waiter, &job));
    CHECK(pthread_join(thread, NULL));
    REQUIRE(job.result == ENOMEM);
    no_waiters(rt, 1);

    atomic_store(&fail_init, 1);
    CHECK(pthread_create(&thread, NULL, read_waiter, &job));
    CHECK(pthread_join(thread, NULL));
    REQUIRE(job.result == EAGAIN);
    no_waiters(rt, 1);

    atomic_store(&fail_wait, 1);
    CHECK(pthread_create(&thread, NULL, read_waiter, &job));
    CHECK(pthread_join(thread, NULL));
    REQUIRE(job.result == EINVAL);
    no_waiters(rt, 1);

    atomic_store(&fail_timed, 1);
    CHECK(pthread_create(&thread, NULL, timed_waiter, &job));
    CHECK(pthread_join(thread, NULL));
    REQUIRE(job.result == EINVAL);
    no_waiters(rt, 1);

    /* Spurious zero-return from both wait variants must not admit a reader
       while the original writer is still holding the lock. */
    for (int mode = 0; mode < 2; ++mode) {
        atomic_store(mode ? &fail_timed : &fail_wait, 3);
        CHECK(pthread_create(&thread, NULL,
                             mode ? timed_waiter : read_waiter, &job));
        wait_for_queue(rt);
        size_t queued = 0, held = 0;
        CHECK(ios27_21d_usage(rt, NULL, &queued, &held));
        REQUIRE(queued == 1 && held == 1);
        CHECK(ios27_21d_unlock(rt, lock));
        CHECK(pthread_join(thread, NULL));
        REQUIRE(job.result == 0);
        no_waiters(rt, 0);
        CHECK(ios27_21d_wrlock(rt, lock));
    }

    /* Signal first, then inject a host wait error after admission. The
       object must release the provisional ownership before returning. */
    atomic_store(&waiting_for_signal, 0);
    atomic_store(&fail_wait, 2);
    CHECK(pthread_create(&thread, NULL, read_waiter, &job));
    wait_for_queue(rt);
    for (unsigned i = 0; i < 3000000u &&
         !atomic_load(&waiting_for_signal); ++i) (void)sched_yield();
    REQUIRE(atomic_load(&waiting_for_signal));
    CHECK(ios27_21d_unlock(rt, lock));
    CHECK(pthread_join(thread, NULL));
    REQUIRE(job.result == EINVAL);
    no_waiters(rt, 0);
    /* Both a queued writer and a timed writer must clear provisional
       exclusive ownership if the host wait fails after a grant. */
    for (int mode = 0; mode < 2; ++mode) {
        atomic_store(&waiting_for_signal, 0);
        atomic_store(mode ? &fail_timed : &fail_wait, 2);
        CHECK(ios27_21d_rdlock(rt, lock));
        CHECK(pthread_create(&thread, NULL,
                             mode ? timed_write_waiter : write_waiter, &job));
        wait_for_queue(rt);
        for (unsigned i = 0; i < 3000000u &&
             !atomic_load(&waiting_for_signal); ++i) (void)sched_yield();
        REQUIRE(atomic_load(&waiting_for_signal));
        CHECK(ios27_21d_unlock(rt, lock));
        CHECK(pthread_join(thread, NULL));
        REQUIRE(job.result == EINVAL);
        no_waiters(rt, 0);
    }
    CHECK(ios27_21d_wrlock(rt, lock));
    CHECK(ios27_21d_unlock(rt, lock));
    /* Force the timed-wait primitive to report a timeout after it actually
       received a valid admission signal. The acquired lock must win. */
    atomic_store(&waiting_for_signal, 0);
    atomic_store(&fail_timed, 4);
    CHECK(ios27_21d_wrlock(rt, lock));
    CHECK(pthread_create(&thread, NULL, timed_waiter, &job));
    wait_for_queue(rt);
    for (unsigned i = 0; i < 3000000u &&
         !atomic_load(&waiting_for_signal); ++i) (void)sched_yield();
    REQUIRE(atomic_load(&waiting_for_signal));
    CHECK(ios27_21d_unlock(rt, lock));
    CHECK(pthread_join(thread, NULL));
    REQUIRE(job.result == 0);
    no_waiters(rt, 0);
    CHECK(ios27_21d_destroy(rt, lock));
    CHECK(ios27_21d_runtime_destroy(rt));
}

typedef struct {
    ios27_21d_runtime *rt;
    ios27_21d_handle once;
    pthread_barrier_t started, finish;
    int published;
} once_data;
static int initialize(void *opaque) {
    once_data *s = opaque;
    int rc = pthread_barrier_wait(&s->started);
    REQUIRE(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    rc = pthread_barrier_wait(&s->finish);
    REQUIRE(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    s->published = 71;
    return 0;
}
static void *initializer(void *opaque) {
    once_data *s = opaque;
    CHECK(ios27_21d_once(s->rt, s->once, initialize, s));
    return NULL;
}
static int unused_initializer(void *ctx) {
    (void)ctx;
    REQUIRE(0 && "once called initializer more than once");
    return EINVAL;
}
static void test_once_fault(void) {
    once_data s = {0};
    pthread_t thread;
    CHECK(ios27_21d_runtime_create(&s.rt, 1));
    CHECK(ios27_21d_create(s.rt, IOS27_21D_ONCE, &s.once));
    CHECK(pthread_barrier_init(&s.started, NULL, 2));
    CHECK(pthread_barrier_init(&s.finish, NULL, 2));
    CHECK(pthread_create(&thread, NULL, initializer, &s));
    int rc = pthread_barrier_wait(&s.started);
    REQUIRE(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    atomic_store(&fail_wait, 1);
    REQUIRE(ios27_21d_once(s.rt, s.once, unused_initializer, NULL) == EINVAL);
    no_waiters(s.rt, 1);
    rc = pthread_barrier_wait(&s.finish);
    REQUIRE(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    CHECK(pthread_join(thread, NULL));
    REQUIRE(s.published == 71);
    CHECK(ios27_21d_once(s.rt, s.once, unused_initializer, NULL));
    CHECK(pthread_barrier_destroy(&s.started));
    CHECK(pthread_barrier_destroy(&s.finish));
    CHECK(ios27_21d_destroy(s.rt, s.once));
    CHECK(ios27_21d_runtime_destroy(s.rt));
}

static int simple_init(void *opaque) {
    int *count = opaque;
    ++*count;
    return 0;
}
static void test_object_create_destroy_rollback(void) {
    ios27_21d_runtime *rt;
    ios27_21d_handle once = {0};
    int called = 0;
    CHECK(ios27_21d_runtime_create(&rt, 1));
    atomic_store(&fail_init, 1);
    REQUIRE(ios27_21d_create(rt, IOS27_21D_ONCE, &once) == EAGAIN);
    REQUIRE(once.slot == 0 && once.runtime_cookie == 0);
    CHECK(ios27_21d_create(rt, IOS27_21D_ONCE, &once));
    atomic_store(&fail_destroy, 1);
    REQUIRE(ios27_21d_destroy(rt, once) == EBUSY);
    REQUIRE(ios27_21d_runtime_destroy(rt) == EBUSY);
    CHECK(ios27_21d_once(rt, once, simple_init, &called));
    REQUIRE(called == 1);
    CHECK(ios27_21d_destroy(rt, once));
    REQUIRE(ios27_21d_once(rt, once, simple_init, &called) == EINVAL);
    CHECK(ios27_21d_runtime_destroy(rt));
}

int main(void) {
    test_rw_faults();
    test_once_fault();
    test_object_create_destroy_rollback();
    puts("phase21d: injected condition init/wait/timed errors rolled back safely");
    return 0;
}
