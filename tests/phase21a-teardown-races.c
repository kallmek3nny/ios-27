#define _XOPEN_SOURCE 700
#include "ios27/libsystem21a_abi.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BASE UINT64_C(0x1200000000)
#define THREAD_COUNT 12u
#define ITERATIONS 4800u
static unsigned char arena[8192];
static ios27_21a_runtime *runtime;
static uint64_t done;
static int invoke(const char *symbol, uint64_t p) {
    uint64_t regs[6] = {p, 0u, 0u, 0u, 0u, 0u}, x0 = UINT64_MAX;
    int e = ios27_21a_call(runtime, symbol, regs, &x0);
    assert(x0 == (uint64_t)e);
    return e;
}
static void *stress_worker(void *arg) {
    uintptr_t id = (uintptr_t)arg;
    unsigned iteration;
    for (iteration = 0; iteration < ITERATIONS; ++iteration) {
        uint64_t m = BASE + 64u, c = BASE + 256u, rw = BASE + 512u;
        int e;
        switch ((iteration + (unsigned)id * 5u) % 11u) {
            case 0:
                e = invoke("pthread_mutex_init", m);
                assert(e == 0 || e == EBUSY || e == EAGAIN);
                break;
            case 1:
                e = invoke("pthread_mutex_trylock", m);
                assert(e == EINVAL || e == EBUSY || e == EDEADLK || e == 0);
                if (!e) assert(invoke("pthread_mutex_unlock", m) == 0);
                break;
            case 2:
                e = invoke("pthread_mutex_destroy", m);
                assert(e == EINVAL || e == EBUSY || e == 0);
                break;
            case 3:
                e = invoke("pthread_cond_init", c);
                assert(e == 0 || e == EBUSY || e == EAGAIN);
                break;
            case 4:
                e = invoke("pthread_cond_signal", c);
                assert(e == 0 || e == EINVAL);
                break;
            case 5:
                e = invoke("pthread_cond_destroy", c);
                assert(e == 0 || e == EBUSY || e == EINVAL);
                break;
            case 6:
                e = invoke("pthread_rwlock_init", rw);
                assert(e == 0 || e == EBUSY || e == EAGAIN);
                break;
            case 7:
                e = invoke("pthread_rwlock_tryrdlock", rw);
                assert(e == 0 || e == EBUSY || e == EINVAL || e == EAGAIN);
                if (!e) assert(invoke("pthread_rwlock_unlock", rw) == 0);
                break;
            case 8:
                e = invoke("pthread_rwlock_trywrlock", rw);
                assert(e == 0 || e == EBUSY || e == EINVAL || e == EAGAIN);
                if (!e) assert(invoke("pthread_rwlock_unlock", rw) == 0);
                break;
            case 9:
                e = invoke("pthread_rwlock_destroy", rw);
                assert(e == 0 || e == EBUSY || e == EINVAL);
                break;
            default:
                e = invoke("pthread_cond_broadcast", c);
                assert(e == 0 || e == EINVAL);
                break;
        }
    }
    __atomic_add_fetch(&done, 1u, __ATOMIC_SEQ_CST);
    return NULL;
}
int main(void) {
    pthread_t threads[THREAD_COUNT];
    unsigned i;
    int e;
    assert(ios27_21a_create(&runtime, arena, sizeof(arena), BASE) == 0);
    for (i = 0; i < THREAD_COUNT; ++i)
        assert(pthread_create(&threads[i], NULL, stress_worker,
                              (void *)(uintptr_t)i) == 0);
    for (i = 0; i < THREAD_COUNT; ++i)
        assert(pthread_join(threads[i], NULL) == 0);
    assert(done == THREAD_COUNT);
    e = invoke("pthread_mutex_destroy", BASE + 64u);
    assert(e == 0 || e == EINVAL);
    e = invoke("pthread_cond_destroy", BASE + 256u);
    assert(e == 0 || e == EINVAL);
    e = invoke("pthread_rwlock_destroy", BASE + 512u);
    assert(e == 0 || e == EINVAL);
    assert(ios27_21a_destroy(runtime) == 0);
    puts("phase21a: 12-way concurrent create/use/destroy races and cleanup passed");
    return 0;
}
