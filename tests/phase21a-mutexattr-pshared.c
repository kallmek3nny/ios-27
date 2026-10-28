#include "ios27/libsystem21a_abi.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BASE UINT64_C(0x1500000000)
static unsigned char guest_memory[8192];
static ios27_21a_runtime *runtime;
static int call(const char *symbol, uint64_t a, uint64_t b) {
    uint64_t args[6] = {a, b, 0, 0, 0, 0};
    uint64_t result = UINT64_MAX;
    int e = ios27_21a_call(runtime, symbol, args, &result);
    assert(result == (uint64_t)e);
    return e;
}
int main(void) {
    const uint64_t attribute = BASE + 64u, mutex = BASE + 256u;
    uint32_t state = UINT32_MAX;
    unsigned i;
    assert(ios27_21a_create(&runtime, guest_memory,
                            sizeof(guest_memory), BASE) == 0);
    assert(call("pthread_mutexattr_init", attribute, 0) == 0);
    assert(call("pthread_mutexattr_getpshared", attribute, BASE + 512u) == 0);
    memcpy(&state, guest_memory + 512u, sizeof(state));
    assert(state == 2u);
    assert(call("pthread_mutexattr_setpshared", attribute, 0u) == EINVAL);
    assert(call("pthread_mutexattr_setpshared", attribute, 3u) == EINVAL);
    assert(call("pthread_mutexattr_setpshared", attribute, 1u) == ENOTSUP);
    assert(call("pthread_mutexattr_getpshared", attribute, attribute + 8u) == EINVAL);
    assert(call("pthread_mutexattr_getpshared", attribute, BASE + 1u) == EFAULT);
    assert(call("pthread_mutexattr_getpshared", attribute,
                BASE + sizeof(guest_memory)) == EFAULT);
    assert(call("pthread_mutexattr_setpshared", attribute, 2u) == 0);
    assert(call("pthread_mutexattr_settype", attribute, 2u) == 0);
    for (i = 0; i < 128u; ++i) {
        assert(call("pthread_mutex_init", mutex, attribute) == 0);
        assert(call("pthread_mutex_lock", mutex, 0) == 0);
        assert(call("pthread_mutex_lock", mutex, 0) == 0);
        assert(call("pthread_mutex_unlock", mutex, 0) == 0);
        assert(call("pthread_mutex_unlock", mutex, 0) == 0);
        assert(call("pthread_mutex_destroy", mutex, 0) == 0);
    }
    assert(call("pthread_mutexattr_destroy", attribute, 0) == 0);
    assert(call("pthread_mutexattr_getpshared", attribute, BASE + 512u) == EINVAL);
    assert(call("pthread_mutex_init", mutex, attribute) == EINVAL);
    assert(ios27_21a_destroy(runtime) == 0);
    puts("phase21a: mutex process-private attributes, recursive semantics and stale attrs passed");
    return 0;
}
