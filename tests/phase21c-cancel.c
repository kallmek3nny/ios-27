#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21c_sync.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "phase21c-cancel %s:%d: %s\n", \
            __FILE__, __LINE__, #expr); abort(); } } while (0)
#define OK(x) CHECK((x) == 0)

typedef struct {
    ios27_21c_runtime *rt;
    ios27_21c_handle mutex, cond;
    pthread_mutex_t ready_guard;
    pthread_cond_t ready_cv;
    int ready;
} cancel_state;
static void announce(cancel_state *s) {
    OK(pthread_mutex_lock(&s->ready_guard));
    s->ready = 1;
    OK(pthread_cond_signal(&s->ready_cv));
    OK(pthread_mutex_unlock(&s->ready_guard));
}
static void await_ready(cancel_state *s) {
    OK(pthread_mutex_lock(&s->ready_guard));
    while (!s->ready) OK(pthread_cond_wait(&s->ready_cv, &s->ready_guard));
    OK(pthread_mutex_unlock(&s->ready_guard));
}
static void release_logical_mutex(void *arg) {
    cancel_state *s = arg;
    OK(ios27_21c_mutex_unlock(s->rt, s->mutex));
}
static void *wait_cancel_worker(void *arg) {
    cancel_state *s = arg;
    OK(ios27_21c_mutex_lock(s->rt, s->mutex));
    pthread_cleanup_push(release_logical_mutex, s);
    announce(s);
    for (;;)
        OK(ios27_21c_cond_wait(s->rt, s->cond, s->mutex));
    pthread_cleanup_pop(1);
    return NULL;
}
static void *lock_cancel_worker(void *arg) {
    cancel_state *s = arg;
    announce(s);
    OK(ios27_21c_mutex_lock(s->rt, s->mutex));
    OK(ios27_21c_mutex_unlock(s->rt, s->mutex));
    return NULL;
}
static void check_cancelled(pthread_t thread) {
    void *value = NULL;
    OK(pthread_join(thread, &value));
    CHECK(value == PTHREAD_CANCELED);
}
int main(void) {
    cancel_state s = {0};
    pthread_t worker;
    OK(ios27_21c_runtime_create(&s.rt, 3));
    OK(ios27_21c_sync_create(s.rt, IOS27_21C_NORMAL_MUTEX, &s.mutex));
    OK(ios27_21c_sync_create(s.rt, IOS27_21C_CONDITION, &s.cond));
    OK(pthread_mutex_init(&s.ready_guard, NULL));
    OK(pthread_cond_init(&s.ready_cv, NULL));
    /* Cancellation of a cond waiter must reacquire the logical mutex,
     * allow its user cleanup to unlock, and drop internal references. */
    OK(pthread_create(&worker, NULL, wait_cancel_worker, &s));
    await_ready(&s);
    OK(ios27_21c_mutex_lock(s.rt, s.mutex));
    OK(pthread_cancel(worker));
    OK(ios27_21c_mutex_unlock(s.rt, s.mutex));
    check_cancelled(worker);
    OK(ios27_21c_sync_destroy(s.rt, s.cond));

    /* Cancellation while queued for mutex acquisition must not strand
     * the internal runtime guard or keep its waiter count alive. */
    s.ready = 0;
    OK(ios27_21c_mutex_lock(s.rt, s.mutex));
    OK(pthread_create(&worker, NULL, lock_cancel_worker, &s));
    await_ready(&s);
    OK(pthread_cancel(worker));
    check_cancelled(worker);
    OK(ios27_21c_mutex_unlock(s.rt, s.mutex));
    OK(ios27_21c_sync_destroy(s.rt, s.mutex));
    OK(pthread_cond_destroy(&s.ready_cv));
    OK(pthread_mutex_destroy(&s.ready_guard));
    OK(ios27_21c_runtime_destroy(s.rt));
    puts("phase21c-cancel: waiting and contended cancellation cleanup passed");
    return 0;
}
