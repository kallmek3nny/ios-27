#define _XOPEN_SOURCE 700
#include "ios27/libsystem21a_abi.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define BASE UINT64_C(0x1300000000)
static unsigned char memory[4096];
static ios27_21a_runtime *rt;
static uint64_t m = BASE + 64u, rw = BASE + 256u, deadline_ptr = BASE + 1024u;
static int invoke(const char *name, uint64_t a, uint64_t b) {
    uint64_t args[6] = {a, b, 0, 0, 0, 0}, x0 = UINT64_MAX;
    int e = ios27_21a_call(rt, name, args, &x0);
    assert(x0 == (uint64_t)e);
    return e;
}
static void deadline_ms(unsigned ms) {
    struct timespec now;
    int64_t seconds, nsecs;
    assert(clock_gettime(CLOCK_REALTIME, &now) == 0);
    seconds = (int64_t)now.tv_sec;
    nsecs = (int64_t)now.tv_nsec + (int64_t)ms * 1000000LL;
    seconds += nsecs / 1000000000LL;
    nsecs %= 1000000000LL;
    memcpy(memory + 1024u, &seconds, sizeof(seconds));
    memcpy(memory + 1032u, &nsecs, sizeof(nsecs));
}
static void *expire_mutex(void *unused) {
    (void)unused;
    assert(invoke("pthread_mutex_timedlock", m, deadline_ptr) == ETIMEDOUT);
    return NULL;
}
static void *expire_writer(void *unused) {
    (void)unused;
    assert(invoke("pthread_rwlock_timedwrlock", rw, deadline_ptr) == ETIMEDOUT);
    return NULL;
}
static void *expire_reader(void *unused) {
    (void)unused;
    assert(invoke("pthread_rwlock_timedrdlock", rw, deadline_ptr) == ETIMEDOUT);
    return NULL;
}
int main(void) {
    pthread_t thread;
    int64_t invalid;
    assert(ios27_21a_create(&rt, memory, sizeof(memory), BASE) == 0);
    assert(invoke("pthread_mutex_init", m, 0) == 0);
    assert(invoke("pthread_rwlock_init", rw, 0) == 0);
    deadline_ms(60u);
    assert(invoke("pthread_mutex_timedlock", m, BASE + 1u) == EFAULT);
    assert(invoke("pthread_rwlock_timedwrlock", rw, BASE + 4096u) == EFAULT);
    invalid = -1;
    memcpy(memory + 1032u, &invalid, sizeof(invalid));
    assert(invoke("pthread_mutex_timedlock", m, deadline_ptr) == EINVAL);
    deadline_ms(60u);
    assert(invoke("pthread_mutex_lock", m, 0) == 0);
    assert(pthread_create(&thread, NULL, expire_mutex, NULL) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(invoke("pthread_mutex_unlock", m, 0) == 0);
    deadline_ms(200u);
    assert(invoke("pthread_mutex_timedlock", m, deadline_ptr) == 0);
    assert(invoke("pthread_mutex_unlock", m, 0) == 0);
    deadline_ms(60u);
    assert(invoke("pthread_rwlock_rdlock", rw, 0) == 0);
    assert(pthread_create(&thread, NULL, expire_writer, NULL) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(invoke("pthread_rwlock_unlock", rw, 0) == 0);
    deadline_ms(60u);
    assert(invoke("pthread_rwlock_wrlock", rw, 0) == 0);
    assert(pthread_create(&thread, NULL, expire_reader, NULL) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(invoke("pthread_rwlock_unlock", rw, 0) == 0);
    deadline_ms(200u);
    assert(invoke("pthread_rwlock_timedrdlock", rw, deadline_ptr) == 0);
    assert(invoke("pthread_rwlock_unlock", rw, 0) == 0);
    assert(invoke("pthread_rwlock_destroy", rw, 0) == 0);
    assert(invoke("pthread_mutex_destroy", m, 0) == 0);
    assert(ios27_21a_destroy(rt) == 0);
    puts("phase21a: mutex and rwlock absolute timedlock, expiry and owner paths passed");
    return 0;
}
