#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21c_sync.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define REQUIRE(x) do { if (!(x)) { \
    fprintf(stderr, "phase21c-races failed %s:%d (%s)\n", \
            __FILE__, __LINE__, #x); abort(); } } while (0)
#define OK(x) REQUIRE((x) == 0)
enum { ATTEMPTS = 700, REUSE_CYCLES = 16000 };
typedef struct {
    ios27_21c_runtime *rt;
    ios27_21c_handle mutex, cond;
    atomic_int sent;
} race_state;
static void *sender(void *arg) {
    race_state *s = arg;
    OK(ios27_21c_mutex_lock(s->rt, s->mutex));
    atomic_store_explicit(&s->sent, 1, memory_order_release);
    OK(ios27_21c_cond_signal(s->rt, s->cond));
    OK(ios27_21c_mutex_unlock(s->rt, s->mutex));
    return NULL;
}
static void test_signal_timeout_races(void) {
    ios27_21c_runtime *rt = NULL;
    ios27_21c_handle mutex, cond;
    race_state state = {0};
    int awakened = 0, expired = 0;
    OK(ios27_21c_runtime_create(&rt, 3));
    OK(ios27_21c_sync_create(rt, IOS27_21C_ERRORCHECK_MUTEX, &mutex));
    OK(ios27_21c_sync_create(rt, IOS27_21C_CONDITION, &cond));
    state.rt = rt; state.mutex = mutex; state.cond = cond;
    atomic_init(&state.sent, 0);
    for (int i = 0; i < ATTEMPTS; ++i) {
        pthread_t worker;
        struct timespec ts;
        int rc;
        atomic_store_explicit(&state.sent, 0, memory_order_relaxed);
        OK(ios27_21c_mutex_lock(rt, mutex));
        OK(pthread_create(&worker, NULL, sender, &state));
        OK(clock_gettime(CLOCK_MONOTONIC, &ts) ? errno : 0);
        ts.tv_nsec += 500000L;
        if (ts.tv_nsec >= 1000000000L) {
            ++ts.tv_sec;
            ts.tv_nsec -= 1000000000L;
        }
        rc = ios27_21c_cond_timedwait(rt, cond, mutex, IOS27_21C_MONOTONIC,
                                       &ts);
        REQUIRE(rc == 0 || rc == ETIMEDOUT);
        if (rc == 0) ++awakened; else ++expired;
        REQUIRE(ios27_21c_mutex_trylock(rt, mutex) == EBUSY);
        OK(ios27_21c_mutex_unlock(rt, mutex));
        OK(pthread_join(worker, NULL));
        REQUIRE(atomic_load_explicit(&state.sent, memory_order_acquire) == 1);
    }
    /* A signal before the wait is not a stored notification. */
    OK(ios27_21c_mutex_lock(rt, mutex));
    OK(ios27_21c_cond_signal(rt, cond));
    struct timespec ts;
    OK(clock_gettime(CLOCK_MONOTONIC, &ts) ? errno : 0);
    ts.tv_sec -= 1;
    REQUIRE(ios27_21c_cond_timedwait(rt, cond, mutex,
                                      IOS27_21C_MONOTONIC, &ts) == ETIMEDOUT);
    OK(ios27_21c_mutex_unlock(rt, mutex));
    REQUIRE(awakened + expired == ATTEMPTS);
    printf("phase21c-races: %d signals and %d timeouts across %d races\n",
           awakened, expired, ATTEMPTS);
    OK(ios27_21c_sync_destroy(rt, cond));
    OK(ios27_21c_sync_destroy(rt, mutex));
    OK(ios27_21c_runtime_destroy(rt));
}

typedef struct {
    ios27_21c_runtime *rt;
    ios27_21c_handle mutex, cond;
    pthread_mutex_t ready_guard;
    pthread_cond_t ready_cv;
    int ready, released;
} association_state;
static void *associated_waiter(void *arg) {
    association_state *s = arg;
    OK(ios27_21c_mutex_lock(s->rt, s->mutex));
    OK(pthread_mutex_lock(&s->ready_guard));
    s->ready = 1;
    OK(pthread_cond_signal(&s->ready_cv));
    OK(pthread_mutex_unlock(&s->ready_guard));
    while (!s->released)
        OK(ios27_21c_cond_wait(s->rt, s->cond, s->mutex));
    OK(ios27_21c_mutex_unlock(s->rt, s->mutex));
    return NULL;
}
static void test_mutex_association(void) {
    association_state s = {0};
    ios27_21c_handle different;
    pthread_t worker;
    struct timespec ts;
    OK(ios27_21c_runtime_create(&s.rt, 4));
    OK(ios27_21c_sync_create(s.rt, IOS27_21C_NORMAL_MUTEX, &s.mutex));
    OK(ios27_21c_sync_create(s.rt, IOS27_21C_NORMAL_MUTEX, &different));
    OK(ios27_21c_sync_create(s.rt, IOS27_21C_CONDITION, &s.cond));
    OK(pthread_mutex_init(&s.ready_guard, NULL));
    OK(pthread_cond_init(&s.ready_cv, NULL));
    OK(pthread_create(&worker, NULL, associated_waiter, &s));
    OK(pthread_mutex_lock(&s.ready_guard));
    while (!s.ready) OK(pthread_cond_wait(&s.ready_cv, &s.ready_guard));
    OK(pthread_mutex_unlock(&s.ready_guard));
    /* Worker must release the logical mutex in the wait before we acquire. */
    OK(ios27_21c_mutex_lock(s.rt, s.mutex));
    OK(ios27_21c_mutex_lock(s.rt, different));
    OK(clock_gettime(CLOCK_MONOTONIC, &ts) ? errno : 0);
    REQUIRE(ios27_21c_cond_timedwait(s.rt, s.cond, different,
                                      IOS27_21C_MONOTONIC, &ts) == EINVAL);
    /* Invalid dynamic association must leave the caller's lock held. */
    REQUIRE(ios27_21c_mutex_trylock(s.rt, different) == EBUSY);
    OK(ios27_21c_mutex_unlock(s.rt, different));
    s.released = 1;
    OK(ios27_21c_cond_signal(s.rt, s.cond));
    OK(ios27_21c_mutex_unlock(s.rt, s.mutex));
    OK(pthread_join(worker, NULL));
    OK(pthread_cond_destroy(&s.ready_cv));
    OK(pthread_mutex_destroy(&s.ready_guard));
    OK(ios27_21c_sync_destroy(s.rt, s.cond));
    OK(ios27_21c_sync_destroy(s.rt, different));
    OK(ios27_21c_sync_destroy(s.rt, s.mutex));
    OK(ios27_21c_runtime_destroy(s.rt));
}
static void test_reuse_cycles(void) {
    ios27_21c_runtime *rt = NULL;
    ios27_21c_handle current, stale;
    OK(ios27_21c_runtime_create(&rt, 1));
    OK(ios27_21c_sync_create(rt, IOS27_21C_NORMAL_MUTEX, &current));
    stale = current;
    for (int i = 0; i < REUSE_CYCLES; ++i) {
        OK(ios27_21c_mutex_lock(rt, current));
        OK(ios27_21c_mutex_unlock(rt, current));
        OK(ios27_21c_sync_destroy(rt, current));
        REQUIRE(ios27_21c_mutex_lock(rt, stale) == EINVAL);
        OK(ios27_21c_sync_create(rt, IOS27_21C_NORMAL_MUTEX, &current));
    }
    REQUIRE(current.generation > stale.generation);
    OK(ios27_21c_sync_destroy(rt, current));
    OK(ios27_21c_runtime_destroy(rt));
}
int main(void) {
    test_signal_timeout_races();
    test_mutex_association();
    test_reuse_cycles();
    puts("phase21c-races: mutex association, signal-timeout races and handle "
         "reuse verified");
    return 0;
}
