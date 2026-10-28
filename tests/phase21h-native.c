#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21h_lifecycle.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "native:%d %s\n", __LINE__, #c); abort(); } } while (0)
#define OK(c) CHECK((c) == 0)

typedef struct { ios27_21h_runtime *rt; ios27_21h_handle h; atomic_int result; } worker_arg;
static void *wrong_owner(void *data) {
    worker_arg *a = data;
    atomic_store(&a->result, ios27_21h_unlock(a->rt, a->h));
    return NULL;
}
static void wait_for(ios27_21h_runtime *rt, size_t wanted) {
    unsigned i;
    for (i = 0; i < 3000000; ++i) {
        ios27_21h_stats st;
        OK(ios27_21h_get_stats(rt, &st));
        if (st.waiting == wanted) return;
        sched_yield();
    }
    CHECK(!"waiter registration timed out");
}
static void *waiter(void *data) {
    worker_arg *a = data;
    int rc = ios27_21h_lock(a->rt, a->h);
    atomic_store(&a->result, rc);
    if (!rc) OK(ios27_21h_unlock(a->rt, a->h));
    return NULL;
}
static void ownership_and_shutdown(void) {
    ios27_21h_runtime *rt = NULL;
    ios27_21h_handle h, other, missing = {0, 0};
    pthread_t worker;
    worker_arg arg;
    ios27_21h_stats st;
    OK(ios27_21h_runtime_create(&rt, 2));
    OK(ios27_21h_object_create(rt, &h));
    OK(ios27_21h_object_create(rt, &other));
    CHECK(ios27_21h_object_create(rt, &missing) == EAGAIN);
    CHECK(missing.slot == 0);
    OK(ios27_21h_lock(rt, h));
    CHECK(ios27_21h_lock(rt, h) == EDEADLK);
    CHECK(ios27_21h_trylock(rt, h) == EBUSY);
    arg = (worker_arg){rt, h, ATOMIC_VAR_INIT(-1)};
    OK(pthread_create(&worker, NULL, wrong_owner, &arg));
    OK(pthread_join(worker, NULL));
    CHECK(atomic_load(&arg.result) == EPERM);
    CHECK(ios27_21h_object_destroy(rt, h) == EBUSY);
    CHECK(ios27_21h_runtime_destroy(rt) == EBUSY);
    OK(ios27_21h_object_close(rt, h));
    CHECK(ios27_21h_object_close(rt, h) == EALREADY);
    CHECK(ios27_21h_lock(rt, h) == ECANCELED);
    CHECK(ios27_21h_trylock(rt, h) == ECANCELED);
    OK(ios27_21h_unlock(rt, h));
    OK(ios27_21h_object_destroy(rt, h));
    CHECK(ios27_21h_unlock(rt, h) == EINVAL);
    OK(ios27_21h_object_create(rt, &missing));
    CHECK(missing.slot == h.slot && missing.generation != h.generation);
    OK(ios27_21h_get_stats(rt, &st));
    CHECK(st.live == 2 && st.waiting == 0 && st.owned == 0);
    OK(ios27_21h_object_destroy(rt, other));
    OK(ios27_21h_object_destroy(rt, missing));
    OK(ios27_21h_get_stats(rt, &st));
    CHECK(st.live == 0 && st.owned == 0 && st.waiting == 0);
    OK(ios27_21h_runtime_destroy(rt));
}
static void close_wakes_all(void) {
    enum { WORKERS = 48 };
    pthread_t tids[WORKERS];
    worker_arg args[WORKERS];
    ios27_21h_runtime *rt = NULL;
    ios27_21h_handle h;
    ios27_21h_stats st;
    OK(ios27_21h_runtime_create(&rt, 1));
    OK(ios27_21h_object_create(rt, &h));
    OK(ios27_21h_lock(rt, h));
    for (int i = 0; i < WORKERS; ++i) {
        args[i].rt = rt; args[i].h = h;
        atomic_init(&args[i].result, -1);
        OK(pthread_create(&tids[i], NULL, waiter, &args[i]));
    }
    wait_for(rt, WORKERS);
    CHECK(ios27_21h_object_destroy(rt, h) == EBUSY);
    OK(ios27_21h_object_close(rt, h));
    OK(ios27_21h_unlock(rt, h));
    for (int i = 0; i < WORKERS; ++i) {
        OK(pthread_join(tids[i], NULL));
        CHECK(atomic_load(&args[i].result) == ECANCELED);
    }
    OK(ios27_21h_get_stats(rt, &st));
    CHECK(st.waiting == 0 && st.owned == 0 && st.closing == 1);
    OK(ios27_21h_object_destroy(rt, h));
    OK(ios27_21h_runtime_destroy(rt));
}
static void recycling(void) {
    enum { LOOPS = 25000 };
    ios27_21h_runtime *rt = NULL;
    ios27_21h_handle h, stale;
    ios27_21h_stats st;
    OK(ios27_21h_runtime_create(&rt, 1));
    OK(ios27_21h_object_create(rt, &h));
    stale = h;
    for (int i = 0; i < LOOPS; ++i) {
        OK(ios27_21h_lock(rt, h));
        OK(ios27_21h_unlock(rt, h));
        OK(ios27_21h_object_destroy(rt, h));
        CHECK(ios27_21h_lock(rt, stale) == EINVAL);
        CHECK(ios27_21h_object_close(rt, h) == EINVAL);
        OK(ios27_21h_object_create(rt, &h));
        CHECK(h.slot == stale.slot && h.generation > stale.generation);
    }
    OK(ios27_21h_object_destroy(rt, h));
    OK(ios27_21h_get_stats(rt, &st));
    CHECK(st.live == 0 && st.waiting == 0 && st.retired == 0);
    OK(ios27_21h_runtime_destroy(rt));
    printf("phase21h-native: %d generations, 48 concurrent waiters, exhausted slots verified\n", LOOPS);
}
int main(void) { ownership_and_shutdown(); close_wakes_all(); recycling(); return 0; }
