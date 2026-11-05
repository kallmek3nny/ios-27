#define _XOPEN_SOURCE 700
#include "ios27/libsystem21a_abi.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BASE UINT64_C(0x200000000)
static unsigned char arena[32768];
static ios27_21a_runtime *rt;
static pthread_barrier_t barrier;
static uint64_t key;
static int dispatch(const char *sym, uint64_t a, uint64_t b, uint64_t *x0) {
    const uint64_t args[6] = {a, b, 0u, 0u, 0u, 0u};
    uint64_t result = UINT64_MAX;
    int e = ios27_21a_call(rt, sym, args, &result);
    *x0 = result;
    if (strcmp(sym, "pthread_getspecific") != 0)
        assert(result == (uint64_t)e);
    return e;
}
static void *worker(void *argument) {
    const unsigned id = (unsigned)(uintptr_t)argument;
    uint64_t x0, addr = BASE + 4096u + (uint64_t)id * 128u;
    int e;
    assert(ios27_21a_attach_thread(rt, BASE + 2048u + (uint64_t)id * 64u) == 0);
    assert(dispatch("pthread_setspecific", key, addr, &x0) == 0);
    e = pthread_barrier_wait(&barrier);
    assert(e == 0 || e == PTHREAD_BARRIER_SERIAL_THREAD);
    assert(dispatch("pthread_getspecific", key, 0, &x0) == 0);
    assert(x0 == addr);
    assert(dispatch("pthread_setspecific", key, 0, &x0) == 0);
    assert(dispatch("pthread_getspecific", key, 0, &x0) == 0 && x0 == 0u);
    assert(dispatch("pthread_setspecific", key, addr, &x0) == 0);
    e = pthread_barrier_wait(&barrier);
    assert(e == 0 || e == PTHREAD_BARRIER_SERIAL_THREAD);
    assert(ios27_21a_detach_thread(rt) == 0);
    return NULL;
}
int main(void) {
    pthread_t threads[8];
    uint64_t x0, old_key, keys[IOS27_21A_MAX_KEYS];
    unsigned i;
    assert(ios27_21a_create(&rt, arena, sizeof(arena), BASE) == 0);
    assert(dispatch("pthread_key_create", BASE + 64u, BASE + 1024u, &x0) == ENOTSUP);
    assert(dispatch("pthread_key_create", BASE + 64u, 0u, &x0) == 0);
    memcpy(&key, arena + 64u, sizeof(key));
    assert(key != 0u);
    assert(dispatch("pthread_getspecific", key, 0u, &x0) == ESRCH && x0 == 0u);
    assert(dispatch("pthread_setspecific", key, BASE + 512u, &x0) == ESRCH);
    assert(dispatch("pthread_setspecific", key, BASE + 999999u, &x0) == EFAULT);
    assert(pthread_barrier_init(&barrier, NULL, 8u) == 0);
    for (i = 0; i < 8u; ++i)
        assert(pthread_create(&threads[i], NULL, worker,
                              (void *)(uintptr_t)i) == 0);
    for (i = 0; i < 8u; ++i)
        assert(pthread_join(threads[i], NULL) == 0);
    assert(pthread_barrier_destroy(&barrier) == 0);
    assert(ios27_21a_destroy(rt) == EBUSY);
    old_key = key;
    assert(dispatch("pthread_key_delete", old_key, 0u, &x0) == 0);
    assert(dispatch("pthread_key_delete", old_key, 0u, &x0) == EINVAL);
    assert(dispatch("pthread_getspecific", old_key, 0u, &x0) == EINVAL && x0 == 0u);
    for (i = 0; i < IOS27_21A_MAX_KEYS; ++i) {
        uint64_t place = BASE + 8000u + (uint64_t)i * 16u;
        assert(dispatch("pthread_key_create", place, 0u, &x0) == 0);
        memcpy(&keys[i], arena + 8000u + i * 16u, sizeof(uint64_t));
        assert(keys[i] != old_key);
    }
    assert(dispatch("pthread_key_create", BASE + 15000u, 0u, &x0) == EAGAIN);
    for (i = 0; i < IOS27_21A_MAX_KEYS; ++i)
        assert(dispatch("pthread_key_delete", keys[i], 0u, &x0) == 0);
    assert(ios27_21a_destroy(rt) == 0);
    puts("phase21a: guest TLS isolation, stale keys, quotas and teardown passed");
    return 0;
}
