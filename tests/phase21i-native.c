#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21i_abi.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21i: %s:%d: %s\n", __FILE__, __LINE__, #x); \
    abort(); } } while (0)
#define OK(x) CHECK((x) == 0)
#define CALL(rt, name, a, b, c) ios27_21i_dispatch((rt), "_pthread_" name, (a), (b), (c))
#define BASE UINT64_C(0x100000000)
#define MUTEX (BASE + 64u)
#define READY (BASE + 192u)
#define RELEASE (BASE + 320u)
#define OTHER (BASE + 448u)

typedef struct {
    ios27_21i_runtime *rt;
    uint64_t mutex;
    int loops;
    int count;
} counter_state;
static void *counter(void *arg) {
    counter_state *s = arg;
    for (int n = 0; n < s->loops; ++n) {
        OK(CALL(s->rt, "mutex_lock", s->mutex, 0, 0));
        ++s->count;
        OK(CALL(s->rt, "mutex_unlock", s->mutex, 0, 0));
    }
    return NULL;
}
typedef struct {
    ios27_21i_runtime *rt;
    int started, go, finished;
} condition_state;
static void *waiter(void *arg) {
    condition_state *s = arg;
    OK(CALL(s->rt, "mutex_lock", MUTEX, 0, 0));
    ++s->started;
    OK(CALL(s->rt, "cond_signal", READY, 0, 0));
    while (!s->go)
        OK(CALL(s->rt, "cond_wait", RELEASE, MUTEX, 0));
    ++s->finished;
    OK(CALL(s->rt, "mutex_unlock", MUTEX, 0, 0));
    return NULL;
}
static void write_le64(uint8_t *dest, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) dest[i] = (uint8_t)(value >> (8u*i));
}
typedef struct {
    ios27_21i_runtime *rt;
    pthread_barrier_t ready;
} cancel_state;
static void *cancelled_mutex_wait(void *arg) {
    cancel_state *s = arg;
    (void)pthread_barrier_wait(&s->ready);
    OK(CALL(s->rt, "mutex_lock", MUTEX, 0, 0));
    OK(CALL(s->rt, "mutex_unlock", MUTEX, 0, 0));
    return NULL;
}
static void cancelled_unlock(void *arg) {
    cancel_state *s = arg;
    OK(CALL(s->rt, "mutex_unlock", MUTEX, 0, 0));
}
static void *cancelled_condition_wait(void *arg) {
    cancel_state *s = arg;
    OK(CALL(s->rt, "mutex_lock", MUTEX, 0, 0));
    (void)pthread_barrier_wait(&s->ready);
    pthread_cleanup_push(cancelled_unlock, s);
    (void)CALL(s->rt, "cond_wait", RELEASE, MUTEX, 0);
    pthread_cleanup_pop(1);
    return NULL;
}
static void test_bridge(void) {
    uint8_t memory[4096] = {0};
    ios27_21i_guest guest = {memory, sizeof(memory), BASE, 64, 48, 8, 8};
    ios27_21i_runtime *rt = NULL;
    ios27_21i_token old, newer;
    ios27_21i_stats stats;
    pthread_t threads[12];
    counter_state counter_state_value = {0};
    condition_state condition = {0};
    struct timespec now;
    CHECK(ios27_21i_create(NULL, &guest, 4) == EINVAL);
    guest.mutex_alignment = 3;
    rt = (ios27_21i_runtime *)(uintptr_t)1;
    CHECK(ios27_21i_create(&rt, &guest, 4) == EINVAL);
    CHECK(rt == NULL);
    guest.mutex_alignment = 8;
    guest.base = UINT64_MAX - 10u;
    CHECK(ios27_21i_create(&rt, &guest, 4) == EINVAL);
    guest.base = BASE;
    OK(ios27_21i_create(&rt, &guest, 4));
    CHECK(ios27_21i_get_stats(rt, NULL) == EINVAL);
    OK(ios27_21i_get_stats(rt, &stats));
    CHECK(stats.capacity == 4 && !stats.mutexes && !stats.conditions &&
          !stats.in_flight && !stats.retired_slots);
    CHECK(CALL(rt, "mutex_init", MUTEX + 1, 0, 0) == EINVAL);
    CHECK(CALL(rt, "mutex_init", BASE + 4080, 0, 0) == EFAULT);
    CHECK(CALL(rt, "mutex_init", UINT64_MAX, 0, 0) == EINVAL);
    CHECK(CALL(rt, "mutex_init", MUTEX, BASE + 1024, 0) == ENOTSUP);
    CHECK(CALL(rt, "mutex_init", MUTEX + 1, BASE + 1024, 0) == EINVAL);
    CHECK(CALL(rt, "bad_symbol", MUTEX, 0, 0) == ENOSYS);
    CHECK(CALL(rt, "mutex_lock", MUTEX, 0, 0) == EINVAL);
    OK(CALL(rt, "mutex_init", MUTEX, 0, 0));
    CHECK(CALL(rt, "cond_init", MUTEX + 32, 0, 0) == EBUSY);
    CHECK(CALL(rt, "mutex_init", MUTEX, 0, 0) == EBUSY);
    OK(CALL(rt, "cond_init", READY, 0, 0));
    OK(CALL(rt, "cond_init", RELEASE, 0, 0));
    OK(CALL(rt, "mutex_init", OTHER, 0, 0));
    CHECK(CALL(rt, "cond_init", BASE + 608, 0, 0) == EAGAIN);
    OK(ios27_21i_get_stats(rt, &stats));
    CHECK(stats.mutexes == 2 && stats.conditions == 2 && !stats.in_flight);
    CHECK(ios27_21i_destroy(rt) == EBUSY);
    OK(ios27_21i_lookup(rt, MUTEX, IOS27_21I_MUTEX, &old));
    newer = (ios27_21i_token){UINT32_MAX, UINT32_MAX};
    CHECK(ios27_21i_lookup(rt, MUTEX, IOS27_21I_CONDITION, &newer) == EINVAL);
    CHECK(newer.slot == 0 && newer.generation == 0);
    OK(ios27_21i_verify(rt, MUTEX, IOS27_21I_MUTEX, old));
    CHECK(CALL(rt, "cond_signal", MUTEX, 0, 0) == EINVAL);
    CHECK(CALL(rt, "mutex_lock", READY, 0, 0) == EINVAL);
    OK(CALL(rt, "mutex_lock", MUTEX, 0, 0));
    CHECK(CALL(rt, "mutex_trylock", MUTEX, 0, 0) == EBUSY);
    CHECK(CALL(rt, "mutex_destroy", MUTEX, 0, 0) == EBUSY);
    OK(CALL(rt, "mutex_unlock", MUTEX, 0, 0));
    CHECK(CALL(rt, "mutex_unlock", MUTEX, 0, 0) == EPERM);
    counter_state_value.rt = rt;
    counter_state_value.mutex = MUTEX;
    counter_state_value.loops = 4000;
    for (int i = 0; i < 12; ++i)
        OK(pthread_create(&threads[i], NULL, counter, &counter_state_value));
    for (int i = 0; i < 12; ++i) OK(pthread_join(threads[i], NULL));
    CHECK(counter_state_value.count == 48000);
    condition.rt = rt;
    for (int i = 0; i < 12; ++i)
        OK(pthread_create(&threads[i], NULL, waiter, &condition));
    OK(CALL(rt, "mutex_lock", MUTEX, 0, 0));
    while (condition.started != 12)
        OK(CALL(rt, "cond_wait", READY, MUTEX, 0));
    OK(ios27_21i_get_stats(rt, &stats));
    CHECK(stats.mutexes == 2 && stats.conditions == 2 &&
          stats.in_flight == 24);
    CHECK(CALL(rt, "cond_destroy", RELEASE, 0, 0) == EBUSY);
    OK(clock_gettime(CLOCK_REALTIME, &now) ? errno : 0);
    write_le64(memory + 1024, (uint64_t)(now.tv_sec - 2));
    write_le64(memory + 1032, 17);
    CHECK(CALL(rt, "cond_timedwait", RELEASE, MUTEX, BASE + 1024) == ETIMEDOUT);
    CHECK(CALL(rt, "mutex_trylock", MUTEX, 0, 0) == EBUSY);
    write_le64(memory + 1032, 1000000000u);
    CHECK(CALL(rt, "cond_timedwait", RELEASE, MUTEX, BASE + 1024) == EINVAL);
    CHECK(CALL(rt, "cond_timedwait", RELEASE, MUTEX, BASE + 4088) == EFAULT);
    condition.go = 1;
    OK(CALL(rt, "cond_broadcast", RELEASE, 0, 0));
    OK(CALL(rt, "mutex_unlock", MUTEX, 0, 0));
    for (int i = 0; i < 12; ++i) OK(pthread_join(threads[i], NULL));
    CHECK(condition.finished == 12);
    OK(ios27_21i_get_stats(rt, &stats));
    CHECK(!stats.in_flight);
    /* Cancellation of a contended mutex must drop bridge in-flight refs. */
    cancel_state cancel = {0};
    pthread_t cancelled_thread;
    void *cancel_result = NULL;
    cancel.rt = rt;
    OK(pthread_barrier_init(&cancel.ready, NULL, 2));
    OK(CALL(rt, "mutex_lock", MUTEX, 0, 0));
    OK(pthread_create(&cancelled_thread, NULL, cancelled_mutex_wait, &cancel));
    (void)pthread_barrier_wait(&cancel.ready);
    OK(pthread_cancel(cancelled_thread));
    OK(pthread_join(cancelled_thread, &cancel_result));
    CHECK(cancel_result == PTHREAD_CANCELED);
    CHECK(CALL(rt, "mutex_destroy", MUTEX, 0, 0) == EBUSY);
    OK(CALL(rt, "mutex_unlock", MUTEX, 0, 0));
    OK(pthread_barrier_destroy(&cancel.ready));
    /* Cond cancellation must restore guest ownership for cleanup handlers. */
    OK(pthread_barrier_init(&cancel.ready, NULL, 2));
    cancel_result = NULL;
    OK(pthread_create(&cancelled_thread, NULL, cancelled_condition_wait, &cancel));
    (void)pthread_barrier_wait(&cancel.ready);
    OK(CALL(rt, "mutex_lock", MUTEX, 0, 0));
    CHECK(CALL(rt, "cond_destroy", RELEASE, 0, 0) == EBUSY);
    OK(CALL(rt, "mutex_unlock", MUTEX, 0, 0));
    OK(pthread_cancel(cancelled_thread));
    OK(pthread_join(cancelled_thread, &cancel_result));
    CHECK(cancel_result == PTHREAD_CANCELED);
    OK(pthread_barrier_destroy(&cancel.ready));
    OK(ios27_21i_get_stats(rt, &stats));
    CHECK(!stats.in_flight);
    OK(CALL(rt, "mutex_destroy", OTHER, 0, 0));
    OK(CALL(rt, "cond_destroy", READY, 0, 0));
    OK(CALL(rt, "cond_destroy", RELEASE, 0, 0));
    OK(CALL(rt, "mutex_destroy", MUTEX, 0, 0));
    CHECK(ios27_21i_verify(rt, MUTEX, IOS27_21I_MUTEX, old) == EINVAL);
    OK(CALL(rt, "mutex_init", MUTEX, 0, 0));
    OK(ios27_21i_lookup(rt, MUTEX, IOS27_21I_MUTEX, &newer));
    CHECK(ios27_21i_verify(rt, MUTEX, IOS27_21I_MUTEX, old) == EINVAL);
    CHECK(newer.generation != old.generation);
    for (int i = 0; i < 300; ++i) {
        OK(CALL(rt, "mutex_destroy", MUTEX, 0, 0));
        OK(CALL(rt, "mutex_init", MUTEX, 0, 0));
    }
    OK(CALL(rt, "mutex_destroy", MUTEX, 0, 0));
    OK(ios27_21i_get_stats(rt, &stats));
    CHECK(!stats.mutexes && !stats.conditions && !stats.in_flight);
    OK(ios27_21i_destroy(rt));
    puts("phase21i: guest bounds, overlap, 48k contested ops, 12 waiters, cancellation cleanup, timed waits, 300 recycling cycles, zero leaked references, and teardown passed");
}
int main(void) { test_bridge(); return 0; }
