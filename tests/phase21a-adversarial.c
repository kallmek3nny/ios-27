#include "ios27/libsystem21a_abi.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BASE UINT64_C(0x1100000000)
#define ROUNDS 60000u
static unsigned char memory[8192];
static uint64_t generator;
static uint64_t random64(void) {
    generator ^= generator << 13;
    generator ^= generator >> 7;
    generator ^= generator << 17;
    return generator;
}
static int call(ios27_21a_runtime *rt, const char *symbol, uint64_t p, uint64_t value) {
    uint64_t args[6] = {p, value, 0, 0, 0, 0}, guest_ret = UINT64_MAX;
    int e = ios27_21a_call(rt, symbol, args, &guest_ret);
    assert(guest_ret == (uint64_t)e);
    return e;
}
static uint64_t run(void) {
    static const char *const names[] = {
        "pthread_mutex_trylock", "pthread_mutex_unlock", "pthread_mutex_init",
        "pthread_mutex_destroy", "pthread_cond_signal", "pthread_cond_broadcast",
        "pthread_cond_init", "pthread_cond_destroy", "pthread_rwlock_tryrdlock",
        "pthread_rwlock_trywrlock", "pthread_rwlock_unlock", "pthread_rwlock_init",
        "pthread_rwlock_destroy", "pthread_mutexattr_gettype",
        "pthread_mutexattr_settype", "pthread_mutexattr_init",
        "pthread_mutexattr_destroy"
    };
    ios27_21a_runtime *rt = NULL;
    uint64_t digest = UINT64_C(0x69a4d3ef);
    unsigned iteration;
    uint64_t mutex = BASE + 64u, cond = BASE + 256u;
    uint64_t rwlock = BASE + 512u, attr = BASE + 1024u;
    memset(memory, 0, sizeof(memory));
    generator = UINT64_C(0x815aabbccddeeff1);
    assert(ios27_21a_create(&rt, memory, sizeof(memory), BASE) == 0);
    assert(call(rt, "pthread_mutex_init", mutex, 0) == 0);
    assert(call(rt, "pthread_cond_init", cond, 0) == 0);
    assert(call(rt, "pthread_rwlock_init", rwlock, 0) == 0);
    assert(call(rt, "pthread_mutexattr_init", attr, 0) == 0);
    for (iteration = 0; iteration < ROUNDS; ++iteration) {
        uint64_t sample = random64();
        unsigned op = (unsigned)(sample % (sizeof(names) / sizeof(names[0])));
        unsigned scenario = (unsigned)((sample >> 16u) % 8u);
        uint64_t addr = op <= 3u ? mutex :
                        op <= 7u ? cond :
                        op <= 12u ? rwlock : attr;
        uint64_t arg = 0;
        int e;
        if (scenario == 1u) addr += 1u;        /* misalignment */
        if (scenario == 2u) addr = BASE - 8u; /* guest underflow */
        if (scenario == 3u) addr = UINT64_MAX - 7u;
        if (scenario == 4u) addr = BASE + sizeof(memory) - 8u;
        if (scenario == 5u && op % 3u) addr = BASE + 256u;
        if (op == 13u) arg = BASE + 4096u;
        if (op == 14u) arg = (sample >> 24u) % 4u;
        e = call(rt, names[op], addr, arg);
        assert(e == 0 || e == EINVAL || e == EFAULT ||
               e == EBUSY || e == EDEADLK || e == EPERM ||
               e == EAGAIN || e == ENOTSUP);
        digest ^= ((uint64_t)(unsigned)e + (uint64_t)op * 97u +
                   (uint64_t)scenario * 16411u + (uint64_t)iteration);
        digest = (digest << 7u) | (digest >> (64u - 7u));
    }
    while (call(rt, "pthread_mutex_unlock", mutex, 0) == 0) {}
    while (call(rt, "pthread_rwlock_unlock", rwlock, 0) == 0) {}
    /* Some randomized destroy actions already closed the object. */
    {
        int e = call(rt, "pthread_mutex_destroy", mutex, 0);
        assert(e == 0 || e == EINVAL);
        e = call(rt, "pthread_cond_destroy", cond, 0);
        assert(e == 0 || e == EINVAL);
        e = call(rt, "pthread_rwlock_destroy", rwlock, 0);
        assert(e == 0 || e == EINVAL);
        e = call(rt, "pthread_mutexattr_destroy", attr, 0);
        assert(e == 0 || e == EINVAL);
    }
    assert(ios27_21a_destroy(rt) == 0);
    return digest;
}
int main(void) {
    uint64_t first = run(), second = run();
    assert(first == second);
    printf("phase21a: %u deterministic adversarial ABI operations twice, digest %llx\n",
           ROUNDS, (unsigned long long)first);
    return 0;
}
