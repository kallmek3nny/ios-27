#include "ios27/libsystem21a_abi.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BASE UINT64_C(0xb00000000)
static unsigned char arena[8192];
static ios27_21a_runtime *rt;
static int invoke(const char *symbol, uint64_t a, uint64_t b) {
    uint64_t args[6] = {a, b, 0, 0, 0, 0}, result = UINT64_MAX;
    int e = ios27_21a_call(rt, symbol, args, &result);
    assert(result == (uint64_t)e);
    return e;
}
static uint64_t read64(size_t off) {
    uint64_t value;
    memcpy(&value, arena + off, sizeof(value));
    return value;
}
static uint32_t read32(size_t off) {
    uint32_t value;
    memcpy(&value, arena + off, sizeof(value));
    return value;
}
int main(void) {
    uint64_t attr = BASE + 64u, output = BASE + 1024u;
    assert(ios27_21a_create(&rt, arena, sizeof(arena), BASE) == 0);
    assert(invoke("pthread_attr_init", attr, 0) == 0);
    assert(invoke("pthread_attr_init", attr, 0) == EBUSY);
    assert(invoke("pthread_attr_getdetachstate", attr, output) == 0);
    assert(read32(1024u) == 1u);
    assert(invoke("pthread_attr_getscope", attr, output + 4u) == 0);
    assert(read32(1028u) == 1u);
    assert(invoke("pthread_attr_getstacksize", attr, output + 8u) == 0);
    assert(read64(1032u) == 512u * 1024u);
    assert(invoke("pthread_attr_getguardsize", attr, output + 16u) == 0);
    assert(read64(1040u) == 16u * 1024u);
    assert(invoke("pthread_attr_setdetachstate", attr, 0u) == EINVAL);
    assert(invoke("pthread_attr_setdetachstate", attr, 2u) == 0);
    assert(invoke("pthread_attr_getdetachstate", attr, output) == 0);
    assert(read32(1024u) == 2u);
    assert(invoke("pthread_attr_setscope", attr, 2u) == ENOTSUP);
    assert(invoke("pthread_attr_setscope", attr, 17u) == EINVAL);
    assert(invoke("pthread_attr_setscope", attr, 1u) == 0);
    assert(invoke("pthread_attr_setstacksize", attr, 100u) == EINVAL);
    assert(invoke("pthread_attr_setstacksize", attr, 65537u) == EINVAL);
    assert(invoke("pthread_attr_setstacksize", attr, 65536u) == 0);
    assert(invoke("pthread_attr_setguardsize", attr, 4096u) == EINVAL);
    assert(invoke("pthread_attr_setguardsize", attr, 0u) == 0);
    assert(invoke("pthread_attr_getstacksize", attr, output + 8u) == 0);
    assert(read64(1032u) == 65536u);
    assert(invoke("pthread_attr_getguardsize", attr, output + 16u) == 0);
    assert(read64(1040u) == 0u);
    assert(invoke("pthread_attr_getstacksize", attr, attr + 16u) == EINVAL);
    assert(invoke("pthread_attr_getdetachstate", attr, BASE + 1u) == EFAULT);
    assert(invoke("pthread_attr_getstacksize", attr, BASE + 9000u) == EFAULT);
    assert(invoke("pthread_attr_destroy", attr, 0) == 0);
    assert(invoke("pthread_attr_destroy", attr, 0) == EINVAL);
    assert(ios27_21a_destroy(rt) == 0);
    puts("phase21a: Darwin attr defaults, stack, guard and detach validation passed");
    return 0;
}
