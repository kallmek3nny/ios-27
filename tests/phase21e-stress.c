#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21e_tls.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "phase21e:%d: %s\n", __LINE__, #expr); abort(); \
} } while (0)
#define WORKERS 16
#define BATCHES 128
static ios27_21e_runtime *runtime;
static ios27_21e_handle primary, secondary, race_key;
static atomic_uint primary_calls, secondary_calls, freed_values;
static atomic_uint race_entered, race_release;

/* No artificial sleeps: all bounded synchronization spins use sched_yield. */
static void until(atomic_uint *flag, unsigned threshold) {
    for (unsigned spin = 0; spin < 30000000u; ++spin) {
        if (atomic_load_explicit(flag, memory_order_acquire) >= threshold) return;
        sched_yield();
    }
    CHECK(0 && "bounded synchronization timed out");
}
typedef struct { unsigned remaining; unsigned number; } allocation;
static allocation *make_value(unsigned depth, unsigned number) {
    allocation *p = malloc(sizeof(*p));
    CHECK(p != NULL);
    p->remaining = depth;
    p->number = number;
    return p;
}
static void primary_destructor(void *pointer) {
    allocation *p = pointer;
    CHECK(p && p->number <= WORKERS * BATCHES);
    atomic_fetch_add(&primary_calls, 1);
    void *current = (void *)(uintptr_t)0x1;
    CHECK(ios27_21e_getspecific(runtime, primary, &current) == 0);
    CHECK(current == NULL); /* cleared before callback */
    if (p->remaining) {
        --p->remaining;
        CHECK(ios27_21e_setspecific(runtime, primary, p) == 0);
    } else {
        free(p);
        atomic_fetch_add(&freed_values, 1);
    }
}
static void secondary_destructor(void *pointer) {
    allocation *p = pointer;
    CHECK(p != NULL);
    atomic_fetch_add(&secondary_calls, 1);
    free(p);
    atomic_fetch_add(&freed_values, 1);
}
static void *tls_worker(void *arg) {
    unsigned n = (unsigned)(uintptr_t)arg;
    CHECK(ios27_21e_attach(runtime) == 0);
    CHECK(ios27_21e_setspecific(runtime, primary, make_value(2, n)) == 0);
    CHECK(ios27_21e_setspecific(runtime, secondary, make_value(0, n)) == 0);
    void *value = NULL;
    CHECK(ios27_21e_getspecific(runtime, primary, &value) == 0);
    CHECK(value != NULL);
    return NULL; /* Native pthread TLS exit hook performs cleanup. */
}
static void test_tls_thread_churn(void) {
    CHECK(ios27_21e_key_create(runtime, primary_destructor, &primary) == 0);
    CHECK(ios27_21e_key_create(runtime, secondary_destructor, &secondary) == 0);
    for (unsigned batch = 0; batch < BATCHES; ++batch) {
        pthread_t threads[WORKERS];
        for (unsigned i = 0; i < WORKERS; ++i)
            CHECK(pthread_create(&threads[i], NULL, tls_worker,
                  (void *)(uintptr_t)(batch * WORKERS + i)) == 0);
        for (unsigned i = 0; i < WORKERS; ++i)
            CHECK(pthread_join(threads[i], NULL) == 0);
    }
    CHECK(atomic_load(&primary_calls) == 3u * WORKERS * BATCHES);
    CHECK(atomic_load(&secondary_calls) == WORKERS * BATCHES);
    CHECK(atomic_load(&freed_values) == 2u * WORKERS * BATCHES);
    size_t attached = 9, keys = 0, tokens = 9;
    uint64_t cleanups = 0, destructors = 0;
    CHECK(ios27_21e_stats(runtime, &attached, &keys, &tokens,
                            &cleanups, &destructors) == 0);
    CHECK(attached == 0 && keys == 2 && tokens == 0);
    CHECK(destructors == 4u * WORKERS * BATCHES);
    CHECK(ios27_21e_key_delete(runtime, primary) == 0);
    CHECK(ios27_21e_key_delete(runtime, secondary) == 0);
    CHECK(ios27_21e_setspecific(runtime, primary, (void *)1) == EINVAL);
    CHECK(ios27_21e_thread_detach(runtime) == EINVAL);
}
static void test_generations_and_exhaustion(void) {
    ios27_21e_handle keys[IOS27_21E_MAX_KEYS], newer, stale;
    void *value = NULL;
    for (unsigned i = 0; i < IOS27_21E_MAX_KEYS; ++i)
        CHECK(ios27_21e_key_create(runtime, NULL, &keys[i]) == 0);
    CHECK(ios27_21e_key_create(runtime, NULL, &newer) == EAGAIN);
    stale = keys[0];
    CHECK(ios27_21e_setspecific(runtime, stale, (void *)(uintptr_t)13) == 0);
    CHECK(ios27_21e_key_delete(runtime, stale) == 0);
    CHECK(ios27_21e_key_create(runtime, NULL, &newer) == 0);
    CHECK(newer.slot == stale.slot && newer.generation != stale.generation);
    CHECK(ios27_21e_getspecific(runtime, stale, &value) == EINVAL);
    CHECK(ios27_21e_setspecific(runtime, stale, &value) == EINVAL);
    CHECK(ios27_21e_getspecific(runtime, newer, &value) == 0 && value == NULL);
    CHECK(ios27_21e_key_delete(runtime, newer) == 0);
    CHECK(ios27_21e_key_delete(runtime, newer) == EINVAL);
    for (unsigned i = 1; i < IOS27_21E_MAX_KEYS; ++i)
        CHECK(ios27_21e_key_delete(runtime, keys[i]) == 0);
    CHECK(ios27_21e_thread_detach(runtime) == 0);
}
static void race_destructor(void *value) {
    atomic_fetch_add_explicit(&race_entered, 1u, memory_order_release);
    until(&race_release, 1);
    free(value);
    atomic_fetch_add(&freed_values, 1);
}
static void *race_worker(void *arg) {
    (void)arg;
    CHECK(ios27_21e_setspecific(runtime, race_key, make_value(0, 0)) == 0);
    return NULL;
}
static void test_inflight_delete(void) {
    CHECK(ios27_21e_key_create(runtime, race_destructor, &race_key) == 0);
    atomic_store(&race_entered, 0);
    atomic_store(&race_release, 0);
    pthread_t worker;
    CHECK(pthread_create(&worker, NULL, race_worker, NULL) == 0);
    until(&race_entered, 1);
    CHECK(ios27_21e_key_delete(runtime, race_key) == EBUSY);
    CHECK(ios27_21e_destroy(runtime) == EBUSY);
    atomic_store_explicit(&race_release, 1, memory_order_release);
    CHECK(pthread_join(worker, NULL) == 0);
    CHECK(ios27_21e_key_delete(runtime, race_key) == 0);
}
typedef struct {
    atomic_uint ready;
    atomic_uint go;
    ios27_21e_handle token;
    unsigned order;
    unsigned tls_destructor_hit;
} cancel_record;
static void cleanup_record(void *value) {
    struct { cancel_record *r; unsigned n; } *item = value;
    item->r->order = item->r->order * 10u + item->n;
}
static void *cancel_worker(void *arg) {
    cancel_record *r = arg;
    struct { cancel_record *r; unsigned n; } items[3] = {
        {r, 1}, {r, 2}, {r, 3}
    };
    uint64_t scopes[3];
    CHECK(ios27_21e_cancel_register(runtime, &r->token) == 0);
    CHECK(ios27_21e_cancel_register(runtime, &r->token) == EALREADY);
    CHECK(ios27_21e_cancel_setstate(runtime, IOS27_21E_CANCEL_DISABLE, NULL) == 0);
    for (unsigned i = 0; i < 3; ++i)
        CHECK(ios27_21e_cleanup_push(runtime, cleanup_record, &items[i], &scopes[i]) == 0);
    atomic_store_explicit(&r->ready, 1, memory_order_release);
    until(&r->go, 1);
    CHECK(ios27_21e_cancel_checkpoint(runtime) == 0);
    CHECK(ios27_21e_cancel_unregister(runtime, r->token) == EBUSY);
    enum ios27_21e_cancel_state old = IOS27_21E_CANCEL_DISABLE;
    CHECK(ios27_21e_cancel_setstate(runtime, IOS27_21E_CANCEL_ENABLE, &old) == 0);
    CHECK(old == IOS27_21E_CANCEL_DISABLE);
    CHECK(ios27_21e_cancel_checkpoint(runtime) == ECANCELED);
    CHECK(ios27_21e_cleanup_pop(runtime, scopes[0], 1) == EINVAL);
    CHECK(ios27_21e_cleanup_unwind(runtime) == 0);
    CHECK(r->order == 321);
    CHECK(ios27_21e_cancel_unregister(runtime, r->token) == 0);
    CHECK(ios27_21e_cancel_request(runtime, r->token) == EINVAL);
    return NULL;
}
static void test_cancel_cleanup(void) {
    cancel_record record = {0};
    pthread_t worker;
    CHECK(pthread_create(&worker, NULL, cancel_worker, &record) == 0);
    until(&record.ready, 1);
    CHECK(ios27_21e_cancel_request(runtime, record.token) == 0);
    CHECK(ios27_21e_cancel_request(runtime, record.token) == 0);
    atomic_store_explicit(&record.go, 1, memory_order_release);
    CHECK(pthread_join(worker, NULL) == 0);
    CHECK(record.order == 321);
    CHECK(ios27_21e_cancel_request(runtime, record.token) == EINVAL);
}
static void test_cleanup_nesting(void) {
    cancel_record record = {0};
    struct { cancel_record *r; unsigned n; } item = {&record, 1};
    uint64_t frames[IOS27_21E_MAX_CLEANUPS], extra = 0;
    for (unsigned i = 0; i < IOS27_21E_MAX_CLEANUPS; ++i)
        CHECK(ios27_21e_cleanup_push(runtime, cleanup_record, &item,
                                     &frames[i]) == 0);
    CHECK(ios27_21e_cleanup_push(runtime, cleanup_record, &item, &extra) == EAGAIN);
    CHECK(ios27_21e_cleanup_pop(runtime, frames[0], 0) == EINVAL);
    for (unsigned i = IOS27_21E_MAX_CLEANUPS; i > 0; --i)
        CHECK(ios27_21e_cleanup_pop(runtime, frames[i - 1], 0) == 0);
    CHECK(record.order == 0);
    CHECK(ios27_21e_thread_detach(runtime) == 0);
}
static atomic_uint quota_ready, quota_release;
static ios27_21e_handle quota_tokens[IOS27_21E_MAX_CANCEL];
static void *quota_worker(void *arg) {
    unsigned n = (unsigned)(uintptr_t)arg;
    CHECK(ios27_21e_cancel_register(runtime, &quota_tokens[n]) == 0);
    atomic_fetch_add_explicit(&quota_ready, 1, memory_order_release);
    until(&quota_release, 1);
    CHECK(ios27_21e_cancel_checkpoint(runtime) == ECANCELED);
    return NULL; /* Token retirement on thread exit. */
}
static void test_token_quota(void) {
    pthread_t threads[IOS27_21E_MAX_CANCEL];
    atomic_store(&quota_ready, 0);
    atomic_store(&quota_release, 0);
    for (unsigned i = 0; i < IOS27_21E_MAX_CANCEL; ++i)
        CHECK(pthread_create(&threads[i], NULL, quota_worker,
                             (void *)(uintptr_t)i) == 0);
    until(&quota_ready, IOS27_21E_MAX_CANCEL);
    ios27_21e_handle extra;
    CHECK(ios27_21e_cancel_register(runtime, &extra) == EAGAIN);
    for (unsigned i = 0; i < IOS27_21E_MAX_CANCEL; ++i)
        CHECK(ios27_21e_cancel_request(runtime, quota_tokens[i]) == 0);
    atomic_store_explicit(&quota_release, 1, memory_order_release);
    for (unsigned i = 0; i < IOS27_21E_MAX_CANCEL; ++i)
        CHECK(pthread_join(threads[i], NULL) == 0);
    CHECK(ios27_21e_cancel_register(runtime, &extra) == 0);
    CHECK(ios27_21e_cancel_unregister(runtime, extra) == 0);
    CHECK(ios27_21e_cancel_request(runtime, quota_tokens[0]) == EINVAL);
    CHECK(ios27_21e_thread_detach(runtime) == 0);
}
int main(void) {
    CHECK(ios27_21e_create(NULL) == EINVAL);
    CHECK(ios27_21e_create(&runtime) == 0);
    test_tls_thread_churn();
    test_generations_and_exhaustion();
    test_inflight_delete();
    test_cancel_cleanup();
    test_cleanup_nesting();
    test_token_quota();
    size_t attached = 1, keys = 1, tokens = 1;
    uint64_t cleanups = 0, destructors = 0;
    CHECK(ios27_21e_stats(runtime, &attached, &keys, &tokens,
                            &cleanups, &destructors) == 0);
    CHECK(attached == 0 && keys == 0 && tokens == 0);
    CHECK(cleanups >= 3u);
    CHECK(destructors == 4u * WORKERS * BATCHES + 1u);
    CHECK(ios27_21e_destroy(runtime) == 0);
    printf("phase21e: %u workers, %u batches, %u cancellation slots, %llu destructors, %llu cleanup handlers; PASS\n",
           WORKERS, BATCHES, IOS27_21E_MAX_CANCEL,
           (unsigned long long)destructors, (unsigned long long)cleanups);
    return 0;
}
