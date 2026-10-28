#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21h_lifecycle.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "races:%d %s\n", __LINE__, #c); abort(); } } while (0)
#define OK(c) CHECK((c) == 0)
enum { WAITERS = 8, ROUNDS = 350 };
typedef struct { ios27_21h_runtime *rt; ios27_21h_handle h; atomic_int *go; int rc; } job;
static void *waiter(void *arg) {
    job *j = arg;
    j->rc = ios27_21h_lock(j->rt, j->h);
    if (!j->rc) OK(ios27_21h_unlock(j->rt, j->h));
    return NULL;
}
static void *closer(void *arg) {
    job *j = arg;
    while (!atomic_load_explicit(j->go, memory_order_acquire)) sched_yield();
    j->rc = ios27_21h_object_close(j->rt, j->h);
    return NULL;
}
static void *destroyer(void *arg) {
    job *j = arg;
    while (!atomic_load_explicit(j->go, memory_order_acquire)) sched_yield();
    j->rc = ios27_21h_object_destroy(j->rt, j->h);
    return NULL;
}
static void wait_count(ios27_21h_runtime *rt, size_t n) {
    for (unsigned i = 0; i < 3000000; ++i) {
        ios27_21h_stats st;
        OK(ios27_21h_get_stats(rt, &st));
        if (st.waiting == n) return;
        sched_yield();
    }
    CHECK(!"bounded waiter setup failed");
}
int main(void) {
    ios27_21h_runtime *rt = NULL;
    ios27_21h_handle previous = {0, 0};
    OK(ios27_21h_runtime_create(&rt, 1));
    for (int round = 0; round < ROUNDS; ++round) {
        pthread_t w[WAITERS], c[2], d;
        job waits[WAITERS], closers[2], destroy;
        ios27_21h_handle current;
        ios27_21h_stats st;
        atomic_int go = ATOMIC_VAR_INIT(0);
        OK(ios27_21h_object_create(rt, &current));
        if (round) {
            CHECK(current.slot == previous.slot && current.generation > previous.generation);
            CHECK(ios27_21h_lock(rt, previous) == EINVAL);
        }
        OK(ios27_21h_lock(rt, current));
        for (int i = 0; i < WAITERS; ++i) {
            waits[i] = (job){rt, current, NULL, -1};
            OK(pthread_create(&w[i], NULL, waiter, &waits[i]));
        }
        wait_count(rt, WAITERS);
        for (int i = 0; i < 2; ++i) {
            closers[i] = (job){rt, current, &go, -1};
            OK(pthread_create(&c[i], NULL, closer, &closers[i]));
        }
        destroy = (job){rt, current, &go, -1};
        OK(pthread_create(&d, NULL, destroyer, &destroy));
        atomic_store_explicit(&go, 1, memory_order_release);
        for (int i = 0; i < 2; ++i) OK(pthread_join(c[i], NULL));
        OK(pthread_join(d, NULL));
        CHECK((closers[0].rc == 0 && closers[1].rc == EALREADY) ||
              (closers[1].rc == 0 && closers[0].rc == EALREADY));
        CHECK(destroy.rc == EBUSY);
        OK(ios27_21h_unlock(rt, current));
        for (int i = 0; i < WAITERS; ++i) {
            OK(pthread_join(w[i], NULL));
            CHECK(waits[i].rc == ECANCELED);
        }
        OK(ios27_21h_get_stats(rt, &st));
        CHECK(st.waiting == 0 && st.owned == 0 && st.closing == 1);
        OK(ios27_21h_object_destroy(rt, current));
        CHECK(ios27_21h_object_destroy(rt, current) == EINVAL);
        previous = current;
    }
    OK(ios27_21h_runtime_destroy(rt));
    printf("phase21h-races: %d close/destroy collisions, %d blocked waiter lifetimes\n",
           ROUNDS, ROUNDS * WAITERS);
    return 0;
}
