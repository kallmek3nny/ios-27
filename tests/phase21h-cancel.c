#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21h_lifecycle.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "cancel:%d %s\n", __LINE__, #c); abort(); } } while (0)
#define OK(c) CHECK((c) == 0)
enum { WORKERS = 32, ROUNDS = 12 };
typedef struct { ios27_21h_runtime *rt; ios27_21h_handle h; } args;
static void *waiter(void *context) {
    args *a = context;
    int rc = ios27_21h_lock(a->rt, a->h);
    if (!rc) OK(ios27_21h_unlock(a->rt, a->h));
    return (void *)(long)rc;
}
static void until_waiting(ios27_21h_runtime *rt, size_t expected) {
    for (unsigned i = 0; i < 3000000; ++i) {
        ios27_21h_stats st;
        OK(ios27_21h_get_stats(rt, &st));
        if (st.waiting == expected) return;
        sched_yield();
    }
    CHECK(!"waiter threshold timeout");
}
int main(void) {
    ios27_21h_runtime *rt = NULL;
    ios27_21h_handle h;
    pthread_t threads[WORKERS];
    args a;
    OK(ios27_21h_runtime_create(&rt, 1));
    for (int round = 0; round < ROUNDS; ++round) {
        ios27_21h_stats st;
        OK(ios27_21h_object_create(rt, &h));
        OK(ios27_21h_lock(rt, h));
        a.rt = rt; a.h = h;
        for (int i = 0; i < WORKERS; ++i)
            OK(pthread_create(&threads[i], NULL, waiter, &a));
        until_waiting(rt, WORKERS);
        CHECK(ios27_21h_object_destroy(rt, h) == EBUSY);
        for (int i = 0; i < WORKERS / 2; ++i)
            OK(pthread_cancel(threads[i]));
        for (int i = 0; i < WORKERS / 2; ++i) {
            void *rv = NULL;
            OK(pthread_join(threads[i], &rv));
            CHECK(rv == PTHREAD_CANCELED);
        }
        until_waiting(rt, WORKERS / 2);
        OK(ios27_21h_object_close(rt, h));
        CHECK(ios27_21h_object_destroy(rt, h) == EBUSY);
        OK(ios27_21h_unlock(rt, h));
        for (int i = WORKERS / 2; i < WORKERS; ++i) {
            void *rv = NULL;
            OK(pthread_join(threads[i], &rv));
            CHECK((long)rv == ECANCELED);
        }
        OK(ios27_21h_get_stats(rt, &st));
        CHECK(st.waiting == 0 && st.owned == 0);
        OK(ios27_21h_object_destroy(rt, h));
        CHECK(ios27_21h_lock(rt, h) == EINVAL);
    }
    OK(ios27_21h_runtime_destroy(rt));
    printf("phase21h-cancel: %d cancellations, %d close wakeups, %d cycles\n",
           ROUNDS * WORKERS / 2, ROUNDS * WORKERS / 2, ROUNDS);
    return 0;
}
