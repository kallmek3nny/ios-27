#include "ios27/libsystem21a_abi.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BASE UINT64_C(0x100000000)
static unsigned char memory[65536];
static ios27_21a_runtime *rt;
static int shared;
static int read_total;
static int call(const char *name, uint64_t a0, uint64_t a1) {
    const uint64_t args[6] = {a0, a1, 0, 0, 0, 0};
    uint64_t result = UINT64_MAX;
    int error = ios27_21a_call(rt, name, args, &result);
    assert(result == (uint64_t)error);
    return error;
}
static void *mutex_worker(void *arg) {
    int i;
    uint64_t addr = *(const uint64_t *)arg;
    for (i = 0; i < 1500; ++i) {
        assert(call("pthread_mutex_lock", addr, 0) == 0);
        ++shared;
        assert(call("pthread_mutex_unlock", addr, 0) == 0);
    }
    return NULL;
}
static void *rw_worker(void *arg) {
    int i;
    uint64_t addr = *(const uint64_t *)arg;
    for (i = 0; i < 700; ++i) {
        assert(call("pthread_rwlock_wrlock", addr, 0) == 0);
        ++read_total;
        assert(call("pthread_rwlock_unlock", addr, 0) == 0);
        assert(call("pthread_rwlock_rdlock", addr, 0) == 0);
        assert(read_total > 0);
        assert(call("pthread_rwlock_unlock", addr, 0) == 0);
    }
    return NULL;
}
static void *invalid_unlock_worker(void *arg) {
    uint64_t *addresses = arg;
    assert(call("pthread_mutex_unlock", addresses[0], 0) == EPERM);
    assert(call("pthread_rwlock_unlock", addresses[1], 0) == EPERM);
    return NULL;
}
int main(void) {
    pthread_t threads[8];
    uint64_t mutex = BASE + 64u, cond = BASE + 256u, rwlock = BASE + 512u;
    uint64_t token, result;
    const uint64_t zeros[6] = {0};
    int i;
    assert(ios27_21a_create(&rt, memory, sizeof(memory), BASE) == 0);
    assert(ios27_21a_validate(rt, mutex, sizeof(ios27_21a_mutex), 8) == 0);
    assert(call("pthread_mutex_init", mutex, 0) == 0);
    assert(call("_pthread_mutex_lock", mutex, 0) == 0);
    assert(call("_pthread_mutex_unlock", mutex, 0) == 0);
    assert(call("__pthread_mutex_lock", mutex, 0) == ENOSYS);
    assert(call("pthread_mutex_init", mutex, 0) == EBUSY);
    assert(call("pthread_mutex_lock", mutex, 0) == 0);
    assert(call("pthread_mutex_trylock", mutex, 0) == EDEADLK ||
           call("pthread_mutex_trylock", mutex, 0) == EBUSY);
    assert(call("pthread_mutex_destroy", mutex, 0) == EBUSY);
    assert(call("pthread_mutex_unlock", mutex, 0) == 0);
    for (i = 0; i < 8; ++i)
        assert(pthread_create(&threads[i], NULL, mutex_worker, &mutex) == 0);
    for (i = 0; i < 8; ++i)
        assert(pthread_join(threads[i], NULL) == 0);
    assert(shared == 12000);
    /* Real Darwin-style static initializer plus 8 concurrent first users. */
    {
        uint64_t static_mutex = BASE + 1024u;
        uint64_t static_cond = BASE + 1280u;
        uint64_t signature = IOS27_21A_MUTEX_STATIC_SIG;
        memcpy(memory + 1024u, &signature, sizeof(signature));
        for (i = 0; i < 8; ++i)
            assert(pthread_create(&threads[i], NULL, mutex_worker, &static_mutex) == 0);
        for (i = 0; i < 8; ++i)
            assert(pthread_join(threads[i], NULL) == 0);
        assert(shared == 24000);
        assert(call("pthread_mutex_destroy", static_mutex, 0) == 0);
        signature = IOS27_21A_COND_STATIC_SIG;
        memcpy(memory + 1280u, &signature, sizeof(signature));
        assert(call("pthread_cond_broadcast", static_cond, 0) == 0);
        assert(call("pthread_cond_destroy", static_cond, 0) == 0);
    }
    assert(call("pthread_cond_init", cond, 0) == 0);
    assert(call("pthread_cond_signal", cond, 0) == 0);
    assert(call("pthread_cond_broadcast", cond, 0) == 0);
    assert(call("pthread_rwlock_init", rwlock, 0) == 0);
    assert(call("pthread_rwlock_rdlock", rwlock, 0) == 0);
    assert(call("pthread_rwlock_wrlock", rwlock, 0) == EDEADLK);
    assert(call("pthread_rwlock_trywrlock", rwlock, 0) == EBUSY);
    assert(call("pthread_rwlock_unlock", rwlock, 0) == 0);
    assert(call("pthread_rwlock_wrlock", rwlock, 0) == 0);
    assert(call("pthread_rwlock_rdlock", rwlock, 0) == EDEADLK);
    assert(call("pthread_rwlock_unlock", rwlock, 0) == 0);
    {
        uint64_t addresses[2] = {mutex, rwlock};
        assert(call("pthread_mutex_lock", mutex, 0) == 0);
        assert(call("pthread_rwlock_wrlock", rwlock, 0) == 0);
        assert(pthread_create(&threads[0], NULL, invalid_unlock_worker, addresses) == 0);
        assert(pthread_join(threads[0], NULL) == 0);
        assert(call("pthread_rwlock_destroy", rwlock, 0) == EBUSY);
        assert(call("pthread_mutex_destroy", mutex, 0) == EBUSY);
        assert(call("pthread_rwlock_unlock", rwlock, 0) == 0);
        assert(call("pthread_mutex_unlock", mutex, 0) == 0);
    }
    for (i = 0; i < 8; ++i)
        assert(pthread_create(&threads[i], NULL, rw_worker, &rwlock) == 0);
    for (i = 0; i < 8; ++i)
        assert(pthread_join(threads[i], NULL) == 0);
    assert(read_total == 5600);
    assert(call("pthread_rwlock_destroy", rwlock, 0) == 0);
    assert(call("pthread_cond_destroy", cond, 0) == 0);
    memcpy(&token, memory + (size_t)(mutex - BASE) + 8u, sizeof(token));
    token ^= UINT64_C(0x55);
    memcpy(memory + (size_t)(mutex - BASE) + 8u, &token, sizeof(token));
    assert(call("pthread_mutex_lock", mutex, 0) == EINVAL);
    token ^= UINT64_C(0x55);
    memcpy(memory + (size_t)(mutex - BASE) + 8u, &token, sizeof(token));
    assert(call("pthread_mutex_destroy", mutex, 0) == 0);
    assert(call("pthread_mutex_lock", mutex, 0) == EINVAL);
    assert(call("pthread_mutex_init", mutex, 0) == 0);
    assert(call("pthread_mutex_destroy", mutex, 0) == 0);
    {
        uint64_t attr = BASE + 1600u;
        uint64_t recursive = BASE + 1664u;
        uint64_t static_recursive = BASE + 1792u;
        uint64_t static_rwlock = BASE + 1920u;
        uint32_t type;
        uint64_t signature;
        assert(call("pthread_mutexattr_init", attr, 0) == 0);
        assert(call("pthread_mutexattr_settype", attr, 9u) == EINVAL);
        assert(call("pthread_mutexattr_settype", attr, 2u) == 0);
        assert(call("pthread_mutexattr_gettype", attr, attr + 8u) == EINVAL);
        assert(call("pthread_mutexattr_gettype", attr, BASE + sizeof(memory)) == EFAULT);
        assert(call("pthread_mutexattr_gettype", attr, BASE + 2200u) == 0);
        memcpy(&type, memory + 2200u, sizeof(type));
        assert(type == 2u);
        assert(call("pthread_mutex_init", recursive, attr) == 0);
        assert(call("pthread_mutex_lock", recursive, 0) == 0);
        assert(call("pthread_mutex_lock", recursive, 0) == 0);
        assert(call("pthread_mutex_destroy", recursive, 0) == EBUSY);
        assert(call("pthread_mutex_unlock", recursive, 0) == 0);
        assert(call("pthread_mutex_unlock", recursive, 0) == 0);
        assert(call("pthread_mutex_destroy", recursive, 0) == 0);
        assert(call("pthread_mutexattr_destroy", attr, 0) == 0);
        signature = IOS27_21A_MUTEX_RECURSIVE_SIG;
        memcpy(memory + 1792u, &signature, sizeof(signature));
        assert(call("pthread_mutex_lock", static_recursive, 0) == 0);
        assert(call("pthread_mutex_lock", static_recursive, 0) == 0);
        assert(call("pthread_mutex_unlock", static_recursive, 0) == 0);
        assert(call("pthread_mutex_unlock", static_recursive, 0) == 0);
        assert(call("pthread_mutex_destroy", static_recursive, 0) == 0);
        signature = IOS27_21A_RWLOCK_STATIC_SIG;
        memcpy(memory + 1920u, &signature, sizeof(signature));
        assert(call("pthread_rwlock_rdlock", static_rwlock, 0) == 0);
        assert(call("pthread_rwlock_unlock", static_rwlock, 0) == 0);
        assert(call("pthread_rwlock_destroy", static_rwlock, 0) == 0);
    }
    assert(call("pthread_create", 0, 0) == ENOTSUP);
    assert(call("unknown_pthread_export", 0, 0) == ENOSYS);
    assert(ios27_21a_call(rt, NULL, zeros, &result) == EINVAL);
    assert(ios27_21a_destroy(rt) == 0);
    puts("phase21a: native contention, opaque lifecycle and ABI dispatch passed");
    return 0;
}
