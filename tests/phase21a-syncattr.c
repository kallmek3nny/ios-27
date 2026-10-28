#include "ios27/libsystem21a_abi.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BASE UINT64_C(0x900000000)
static unsigned char arena[8192];
static ios27_21a_runtime *rt;
static int invoke(const char *symbol, uint64_t a, uint64_t b) {
    uint64_t args[6] = {a, b, 0, 0, 0, 0}, result = UINT64_MAX;
    int e = ios27_21a_call(rt, symbol, args, &result);
    assert(result == (uint64_t)e);
    return e;
}
static uint32_t value32(size_t offset) {
    uint32_t value = 0;
    memcpy(&value, arena + offset, sizeof(value));
    return value;
}
int main(void) {
    uint64_t ca = BASE + 64u, ra = BASE + 128u;
    uint64_t co = BASE + 256u, rw = BASE + 512u;
    assert(ios27_21a_create(&rt, arena, sizeof(arena), BASE) == 0);
    assert(invoke("pthread_condattr_init", ca, 0) == 0);
    assert(invoke("pthread_rwlockattr_init", ra, 0) == 0);
    assert(invoke("pthread_condattr_getpshared", ca, BASE + 1024u) == 0);
    assert(value32(1024u) == 2u);
    assert(invoke("pthread_rwlockattr_getpshared", ra, BASE + 1056u) == 0);
    assert(value32(1056u) == 2u);
    assert(invoke("pthread_condattr_setpshared", ca, 1u) == ENOTSUP);
    assert(invoke("pthread_rwlockattr_setpshared", ra, 1u) == ENOTSUP);
    assert(invoke("pthread_condattr_setpshared", ca, 55u) == EINVAL);
    assert(invoke("pthread_rwlockattr_setpshared", ra, 55u) == EINVAL);
    assert(invoke("pthread_condattr_setpshared", ca, 2u) == 0);
    assert(invoke("pthread_rwlockattr_setpshared", ra, 2u) == 0);
    assert(invoke("pthread_condattr_getpshared", ca, ca + 8u) == EINVAL);
    assert(invoke("pthread_condattr_getpshared", ca, BASE + 8192u) == EFAULT);
    assert(invoke("pthread_cond_init", co, ra) == EINVAL);
    assert(invoke("pthread_rwlock_init", rw, ca) == EINVAL);
    assert(invoke("pthread_cond_init", co, ca) == 0);
    assert(invoke("pthread_rwlock_init", rw, ra) == 0);
    assert(invoke("pthread_cond_signal", co, 0) == 0);
    assert(invoke("pthread_rwlock_rdlock", rw, 0) == 0);
    assert(invoke("pthread_rwlock_unlock", rw, 0) == 0);
    assert(invoke("pthread_cond_destroy", co, 0) == 0);
    assert(invoke("pthread_rwlock_destroy", rw, 0) == 0);
    assert(invoke("pthread_condattr_destroy", ca, 0) == 0);
    assert(invoke("pthread_rwlockattr_destroy", ra, 0) == 0);
    assert(invoke("pthread_condattr_destroy", ca, 0) == EINVAL);
    assert(invoke("pthread_rwlockattr_destroy", ra, 0) == EINVAL);
    assert(ios27_21a_destroy(rt) == 0);
    puts("phase21a: Darwin private condition/rwlock attributes and failure paths passed");
    return 0;
}
