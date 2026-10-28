#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21e_tls.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(e) do { if (!(e)) { \
    fprintf(stderr, "phase21e teardown:%d: %s\n", __LINE__, #e); abort(); \
} } while(0)
static ios27_21e_runtime *r;
static ios27_21e_handle key;
static atomic_uint calls, cleanup_runs, stage;
static void dtor_four_passes(void *p) {
    CHECK(p == (void *)&stage);
    unsigned n = atomic_fetch_add(&calls, 1) + 1;
    if (n < IOS27_21E_DESTRUCTOR_PASSES + 2u)
        CHECK(ios27_21e_setspecific(r, key, p) == 0);
    /* Beyond fourth invocation, key value remains opaque and no further
     * destructor is allowed. Intentionally static storage, not leaked heap. */
}
static void cleanup_before_dtor(void *p) {
    CHECK(p == (void *)&stage);
    CHECK(atomic_load(&calls) == 0);
    atomic_fetch_add(&cleanup_runs, 1);
    CHECK(ios27_21e_thread_detach(r) == EBUSY);
}
static void *worker(void *p) {
    (void)p;
    uint64_t scope;
    CHECK(ios27_21e_setspecific(r, key, &stage) == 0);
    CHECK(ios27_21e_cleanup_push(r, cleanup_before_dtor, &stage, &scope) == 0);
    CHECK(ios27_21e_destroy(r) == EBUSY);
    return NULL;
}
static void test_exit_teardown(void) {
    CHECK(ios27_21e_key_create(r, dtor_four_passes, &key) == 0);
    pthread_t t;
    CHECK(pthread_create(&t, NULL, worker, NULL) == 0);
    CHECK(pthread_join(t, NULL) == 0);
    CHECK(atomic_load(&calls) == IOS27_21E_DESTRUCTOR_PASSES);
    CHECK(atomic_load(&cleanup_runs) == 1);
    CHECK(ios27_21e_key_delete(r, key) == 0);
}
static void test_manual_detach(void) {
    atomic_store(&calls, 0);
    CHECK(ios27_21e_key_create(r, dtor_four_passes, &key) == 0);
    CHECK(ios27_21e_setspecific(r, key, &stage) == 0);
    CHECK(ios27_21e_thread_detach(r) == 0);
    CHECK(atomic_load(&calls) == IOS27_21E_DESTRUCTOR_PASSES);
    CHECK(ios27_21e_thread_detach(r) == EINVAL);
    CHECK(ios27_21e_key_delete(r, key) == 0);
}
static void test_stale_cancel_token(void) {
    ios27_21e_handle first, next;
    CHECK(ios27_21e_cancel_register(r, &first) == 0);
    CHECK(ios27_21e_cancel_request(r, first) == 0);
    CHECK(ios27_21e_cancel_checkpoint(r) == ECANCELED);
    CHECK(ios27_21e_cancel_unregister(r, first) == 0);
    CHECK(ios27_21e_cancel_register(r, &next) == 0);
    CHECK(first.slot == next.slot && first.generation != next.generation);
    CHECK(ios27_21e_cancel_request(r, first) == EINVAL);
    CHECK(ios27_21e_cancel_checkpoint(r) == 0);
    CHECK(ios27_21e_cancel_unregister(r, next) == 0);
    CHECK(ios27_21e_thread_detach(r) == 0);
}
static atomic_uint setter_go, setter_done;
static atomic_int setter_code;
static void *setter(void *ignored) {
    (void)ignored;
    while (!atomic_load_explicit(&setter_go, memory_order_acquire)) sched_yield();
    ios27_21e_handle stale = key;
    atomic_store(&setter_code, ios27_21e_setspecific(r, stale, &stage));
    atomic_store_explicit(&setter_done, 1, memory_order_release);
    return NULL;
}
static void test_deleted_key_no_attached_context(void) {
    CHECK(ios27_21e_key_create(r, NULL, &key) == 0);
    pthread_t t;
    atomic_store(&setter_go, 0);
    atomic_store(&setter_done, 0);
    CHECK(pthread_create(&t, NULL, setter, NULL) == 0);
    CHECK(ios27_21e_key_delete(r, key) == 0);
    atomic_store_explicit(&setter_go, 1, memory_order_release);
    CHECK(pthread_join(t, NULL) == 0);
    CHECK(atomic_load(&setter_done) == 1);
    CHECK(atomic_load(&setter_code) == EINVAL);
    size_t attached = 9;
    CHECK(ios27_21e_stats(r, &attached, NULL, NULL, NULL, NULL) == 0);
    CHECK(attached == 0);
}
int main(void) {
    CHECK(ios27_21e_create(&r) == 0);
    test_exit_teardown();
    test_manual_detach();
    test_stale_cancel_token();
    test_deleted_key_no_attached_context();
    CHECK(ios27_21e_destroy(r) == 0);
    puts("phase21e teardown: destructor iteration cap, LIFO exit cleanup, manual detach, generation reuse and rollback PASS");
    return 0;
}
