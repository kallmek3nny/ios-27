#include "ios27/libsystem21a_abi.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BASE UINT64_C(0x700000000)
static unsigned char bytes[65536];
static int invoke(ios27_21a_runtime *rt, const char *name, uint64_t p, uint64_t a) {
    uint64_t regs[6] = {p, a, 0, 0, 0, 0}, ret = UINT64_MAX;
    int e = ios27_21a_call(rt, name, regs, &ret);
    assert(ret == (uint64_t)e);
    return e;
}
int main(void) {
    ios27_21a_runtime *rt;
    unsigned i;
    uint64_t p, sig;
    assert(ios27_21a_create(NULL, bytes, sizeof(bytes), BASE) == EINVAL);
    assert(ios27_21a_create(&rt, bytes, sizeof(bytes), UINT64_MAX - 1u) == EINVAL);
    assert(ios27_21a_create(&rt, bytes, sizeof(bytes), BASE) == 0);
    assert(ios27_21a_validate(rt, BASE - 8u, 8, 8) == EFAULT);
    assert(ios27_21a_validate(rt, UINT64_MAX - 7u, 16, 8) == EFAULT);
    assert(ios27_21a_validate(rt, BASE + 1u, 8, 8) == EFAULT);
    assert(ios27_21a_validate(rt, BASE, 8, 3) == EINVAL);
    assert(invoke(rt, "pthread_mutex_init", BASE + 1u, 0) == EFAULT);
    assert(invoke(rt, "pthread_mutex_init", BASE + sizeof(bytes) - 8u, 0) == EFAULT);
    assert(invoke(rt, "pthread_mutex_init", BASE + 64u, 1u) == ENOTSUP);
    assert(invoke(rt, "pthread_mutex_unlock", BASE + 64u, 0) == EINVAL);
    assert(invoke(rt, "pthread_cond_signal", BASE + 64u, 0) == EINVAL);
    assert(invoke(rt, "pthread_rwlock_wrlock", BASE + 64u, 0) == EINVAL);
    sig = IOS27_21A_MUTEX_STATIC_SIG;
    memcpy(bytes + 64, &sig, sizeof(sig));
    assert(invoke(rt, "pthread_mutex_init", BASE + 64u, 0) == EBUSY);
    memset(bytes + 64, 0, 64);
    for (i = 0; i < IOS27_21A_MAX_OBJECTS; ++i) {
        p = BASE + 512u + (uint64_t)i * 256u;
        assert(invoke(rt, "pthread_mutex_init", p, 0) == 0);
        if (i == 0u) {
            assert(invoke(rt, "pthread_cond_init", p + 32u, 0) == EBUSY);
            assert(invoke(rt, "pthread_mutex_lock", p, 0) == 0);
            assert(invoke(rt, "pthread_mutex_unlock", p, 0) == 0);
        }
    }
    assert(invoke(rt, "pthread_mutex_init", BASE + 512u +
                  (uint64_t)IOS27_21A_MAX_OBJECTS * 256u, 0) == EAGAIN);
    assert(ios27_21a_destroy(rt) == EBUSY);
    for (i = 0; i < IOS27_21A_MAX_OBJECTS; ++i) {
        p = BASE + 512u + (uint64_t)i * 256u;
        assert(invoke(rt, "pthread_mutex_destroy", p, 0) == 0);
    }
    for (i = 0; i < 10000; ++i) {
        p = BASE + 512u;
        assert(invoke(rt, "pthread_mutex_init", p, 0) == 0);
        assert(invoke(rt, "pthread_mutex_destroy", p, 0) == 0);
    }
    assert(ios27_21a_destroy(rt) == 0);
    puts("phase21a: bounds, alignment, capacity, rollback and 10000 reuse cycles passed");
    return 0;
}
