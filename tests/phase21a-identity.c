#define _XOPEN_SOURCE 700
#include "ios27/libsystem21a_abi.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#define BASE UINT64_C(0x300000000)
static unsigned char arena[8192];
static ios27_21a_runtime *rt;
static pthread_barrier_t barrier;
static uint64_t addresses[8];
static int abi_self(uint64_t *result) {
    uint64_t x0 = UINT64_MAX;
    uint64_t args[6] = {0, 0, 0, 0, 0, 0};
    int e = ios27_21a_call(rt, "_pthread_self", args, &x0);
    *result = x0;
    return e;
}
static int abi_equal(uint64_t a, uint64_t b, uint64_t *result) {
    uint64_t x0 = UINT64_MAX;
    uint64_t args[6] = {a, b, 0, 0, 0, 0};
    int e = ios27_21a_call(rt, "_pthread_equal", args, &x0);
    *result = x0;
    return e;
}
static void *worker(void *argument) {
    size_t id = (size_t)(uintptr_t)argument, i;
    uint64_t self = 0, eq = 0;
    int e;
    assert(ios27_21a_attach_thread(rt, addresses[id]) == 0);
    assert(abi_self(&self) == 0 && self == addresses[id]);
    assert(ios27_21a_attach_thread(rt, addresses[id]) == EEXIST);
    e = pthread_barrier_wait(&barrier);
    assert(e == 0 || e == PTHREAD_BARRIER_SERIAL_THREAD);
    for (i = 0; i < 8; ++i) {
        assert(abi_equal(self, addresses[i], &eq) == 0);
        assert(eq == (uint64_t)(i == id));
    }
    e = pthread_barrier_wait(&barrier);
    assert(e == 0 || e == PTHREAD_BARRIER_SERIAL_THREAD);
    assert(ios27_21a_detach_thread(rt) == 0);
    assert(ios27_21a_detach_thread(rt) == ESRCH);
    assert(abi_self(&self) == ESRCH && self == 0u);
    assert(ios27_21a_attach_thread(rt, addresses[id]) == EEXIST);
    return NULL;
}
int main(void) {
    pthread_t threads[8];
    uint64_t self = 99u, eq = 99u;
    size_t i;
    assert(ios27_21a_create(&rt, arena, sizeof(arena), BASE) == 0);
    assert(abi_self(&self) == ESRCH && self == 0u);
    assert(abi_equal(BASE + 64u, BASE + 64u, &eq) == ESRCH && eq == 0u);
    assert(ios27_21a_attach_thread(rt, BASE + 1u) == EFAULT);
    assert(ios27_21a_attach_thread(rt, BASE + sizeof(arena) + 64u) == EFAULT);
    for (i = 0; i < 8; ++i) addresses[i] = BASE + 64u + (uint64_t)i * 64u;
    assert(pthread_barrier_init(&barrier, NULL, 8u) == 0);
    for (i = 0; i < 8; ++i)
        assert(pthread_create(&threads[i], NULL, worker, (void *)(uintptr_t)i) == 0);
    for (i = 0; i < 8; ++i)
        assert(pthread_join(threads[i], NULL) == 0);
    assert(pthread_barrier_destroy(&barrier) == 0);
    assert(abi_equal(addresses[0], addresses[0], &eq) == ESRCH);
    assert(ios27_21a_attach_thread(rt, BASE + 1024u) == 0);
    {
        uint64_t args[6] = {BASE + 1024u, 0, 0, 0, 0, 0};
        uint64_t x0 = UINT64_MAX;
        assert(ios27_21a_call(rt, "pthread_mutex_init", args, &x0) == EBUSY);
        assert(x0 == (uint64_t)EBUSY);
    }
    assert(abi_self(&self) == 0 && self == BASE + 1024u);
    assert(ios27_21a_destroy(rt) == EBUSY);
    assert(ios27_21a_detach_thread(rt) == 0);
    assert(ios27_21a_destroy(rt) == 0);
    puts("phase21a: concurrent guest pthread identity and stale-pointer bans passed");
    return 0;
}
