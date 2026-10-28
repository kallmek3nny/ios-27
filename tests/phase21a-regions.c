#include "ios27/libsystem21a_abi.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BASE UINT64_C(0x600000000)
static unsigned char first[256], readonly[512], last[1024];
static int dispatch(ios27_21a_runtime *rt, const char *symbol, uint64_t addr) {
    uint64_t regs[6] = {addr, 0, 0, 0, 0, 0};
    uint64_t result = UINT64_MAX;
    int e = ios27_21a_call(rt, symbol, regs, &result);
    assert(result == (uint64_t)e);
    return e;
}
int main(void) {
    ios27_21a_runtime *rt = NULL;
    ios27_21a_region regions[3] = {
        {first, sizeof(first), BASE, IOS27_21A_READ | IOS27_21A_WRITE},
        {readonly, sizeof(readonly), BASE + 256u, IOS27_21A_READ},
        {last, sizeof(last), BASE + 1024u, IOS27_21A_READ | IOS27_21A_WRITE}
    };
    assert(ios27_21a_create_regions(&rt, regions, 3) == 0);
    assert(ios27_21a_validate_access(rt, BASE + 256u, 32u, 8u,
                                      IOS27_21A_READ) == 0);
    assert(ios27_21a_validate_access(rt, BASE + 256u, 32u, 8u,
                                      IOS27_21A_WRITE) == EFAULT);
    assert(ios27_21a_validate(rt, BASE + 256u, 32u, 8u) == EFAULT);
    assert(ios27_21a_validate(rt, BASE + 224u, 64u, 8u) == EFAULT);
    assert(ios27_21a_validate(rt, BASE + 768u, 1u, 1u) == EFAULT);
    assert(ios27_21a_validate_access(rt, BASE + 1024u, 64u, 8u,
                                      IOS27_21A_READ | IOS27_21A_WRITE) == 0);
    assert(ios27_21a_validate_access(rt, BASE + 1024u, 1u, 8u, 0u) == EINVAL);
    assert(dispatch(rt, "pthread_mutex_init", BASE + 256u) == EFAULT);
    assert(dispatch(rt, "pthread_mutex_init", BASE + 224u) == EFAULT);
    assert(dispatch(rt, "pthread_mutex_init", BASE + 1024u) == 0);
    assert(dispatch(rt, "pthread_mutex_lock", BASE + 1024u) == 0);
    assert(dispatch(rt, "pthread_mutex_unlock", BASE + 1024u) == 0);
    assert(dispatch(rt, "pthread_mutex_destroy", BASE + 1024u) == 0);
    assert(ios27_21a_destroy(rt) == 0);
    rt = NULL;
    regions[2].guest_base = BASE + 300u;
    assert(ios27_21a_create_regions(&rt, regions, 3) == EINVAL);
    assert(rt == NULL);
    regions[2].guest_base = BASE + 1024u;
    regions[2].host = first + 128u;
    regions[2].size = 128u;
    assert(ios27_21a_create_regions(&rt, regions, 3) == EINVAL);
    regions[2].host = last;
    regions[2].size = sizeof(last);
    regions[2].permissions = IOS27_21A_WRITE;
    assert(ios27_21a_create_regions(&rt, regions, 3) == EINVAL);
    regions[2].permissions = IOS27_21A_READ | IOS27_21A_WRITE;
    regions[2].guest_base = UINT64_MAX - 1u;
    assert(ios27_21a_create_regions(&rt, regions, 3) == EINVAL);
    assert(rt == NULL);
    puts("phase21a: permissioned regions, aliases and boundary straddles passed");
    return 0;
}
