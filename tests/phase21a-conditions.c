#define _XOPEN_SOURCE 700
#include "ios27/libsystem21a_abi.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BASE UINT64_C(0x500000000)
static unsigned char memory[4096];
static ios27_21a_runtime *rt;
static pthread_barrier_t barrier;
static int ready;
static const uint64_t mutex = BASE + 64u, condition = BASE + 256u;
static int invoke(const char *symbol, uint64_t a, uint64_t b, uint64_t c) {
    uint64_t regs[6] = {a, b, c, 0, 0, 0}, ret = UINT64_MAX;
    int error = ios27_21a_call(rt, symbol, regs, &ret);
    assert(ret == (uint64_t)error);
    return error;
}
static void *waiter(void *unused) {
    int e;
    (void)unused;
    assert(invoke("pthread_mutex_lock", mutex, 0, 0) == 0);
    e = pthread_barrier_wait(&barrier);
    assert(e == 0 || e == PTHREAD_BARRIER_SERIAL_THREAD);
    while (!ready)
        assert(invoke("pthread_cond_wait", condition, mutex, 0) == 0);
    assert(invoke("pthread_mutex_unlock", mutex, 0, 0) == 0);
    return NULL;
}
int main(void) {
    pthread_t thread;
    int e;
    uint64_t nanoseconds;
    assert(ios27_21a_create(&rt, memory, sizeof(memory), BASE) == 0);
    assert(invoke("pthread_mutex_init", mutex, 0, 0) == 0);
    assert(invoke("pthread_cond_init", condition, 0, 0) == 0);
    assert(invoke("pthread_cond_wait", condition, mutex, 0) == EPERM);
    assert(invoke("pthread_mutex_lock", mutex, 0, 0) == 0);
    assert(invoke("pthread_cond_timedwait", condition, mutex, BASE + 512u) == ETIMEDOUT);
    nanoseconds = UINT64_C(1000000000);
    memcpy(memory + 520u, &nanoseconds, sizeof(nanoseconds));
    assert(invoke("pthread_cond_timedwait", condition, mutex, BASE + 512u) == EINVAL);
    assert(invoke("pthread_cond_timedwait", condition, mutex, BASE + 513u) == EFAULT);
    assert(invoke("pthread_mutex_unlock", mutex, 0, 0) == 0);
    assert(pthread_barrier_init(&barrier, NULL, 2u) == 0);
    assert(pthread_create(&thread, NULL, waiter, NULL) == 0);
    e = pthread_barrier_wait(&barrier);
    assert(e == 0 || e == PTHREAD_BARRIER_SERIAL_THREAD);
    /* Waiter owns the mutex at barrier; main can lock only once wait starts. */
    assert(invoke("pthread_mutex_lock", mutex, 0, 0) == 0);
    assert(invoke("pthread_cond_destroy", condition, 0, 0) == EBUSY);
    assert(invoke("pthread_mutex_destroy", mutex, 0, 0) == EBUSY);
    ready = 1;
    assert(invoke("pthread_cond_signal", condition, 0, 0) == 0);
    assert(invoke("pthread_mutex_unlock", mutex, 0, 0) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(pthread_barrier_destroy(&barrier) == 0);
    assert(invoke("pthread_cond_destroy", condition, 0, 0) == 0);
    assert(invoke("pthread_mutex_destroy", mutex, 0, 0) == 0);
    assert(ios27_21a_destroy(rt) == 0);
    puts("phase21a: cond wait/signal handoff, timed expiry and owner checks passed");
    return 0;
}
