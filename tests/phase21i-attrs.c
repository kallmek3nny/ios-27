#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21i_abi.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BASE UINT64_C(0x400000000)
#define M (BASE + 64u)
#define C (BASE + 192u)
#define MATTR (BASE + 512u)
#define CATTR (BASE + 640u)
#define ASSERT(x) do { if (!(x)) { \
    fprintf(stderr, "phase21i attributes %s:%d: %s\\n", \
            __FILE__, __LINE__, #x); abort(); } \
} while (0)
#define OK(x) ASSERT((x) == 0)
#define CALL(rt, name, a, b) ios27_21i_dispatch(rt, "_pthread_" name, a, b, 0)

static void le32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8u * i));
}
static void le64(uint8_t *p, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) p[i] = (uint8_t)(value >> (8u * i));
}
/* Apple libpthread src/types_internal.h:
 * mutexattr sig=0x4d545841, int ceiling at +8, flags at +12;
 * flags protocol:2, type:2, pshared:2, policy:3 (low to high).
 * condattr sig=0x434e4441, pshared:2 at +8. */
static void set_mutexattr(uint8_t *mem, unsigned kind, unsigned pshared,
                          unsigned protocol, unsigned policy, uint32_t ceiling) {
    memset(mem + 512, 0, 16);
    le64(mem + 512, UINT64_C(0x4d545841));
    le32(mem + 520, ceiling);
    le32(mem + 524, (protocol & 3u) | ((kind & 3u) << 2u) |
                    ((pshared & 3u) << 4u) | ((policy & 7u) << 6u));
}
static void set_condattr(uint8_t *mem, unsigned pshared) {
    memset(mem + 640, 0, 16);
    le64(mem + 640, UINT64_C(0x434e4441));
    le32(mem + 648, pshared & 3u);
}
static void assert_unused(ios27_21i_runtime *rt) {
    ios27_21i_stats stats;
    OK(ios27_21i_get_stats(rt, &stats));
    ASSERT(stats.mutexes == 0 && stats.conditions == 0 && stats.in_flight == 0);
}
int main(void) {
    uint8_t memory[2048] = {0};
    ios27_21i_runtime *runtime = NULL;
    ios27_21i_guest guest = {memory, sizeof(memory), BASE, 64, 48, 8, 8};
    ios27_21i_guest mismatched = guest;
    mismatched.mutex_extent = 48;
    runtime = (ios27_21i_runtime *)(uintptr_t)1;
    ASSERT(ios27_21i_create_apple_lp64_attrs(&runtime, &mismatched, 4) == EINVAL);
    ASSERT(runtime == NULL);
    OK(ios27_21i_create(&runtime, &guest, 4));
    set_mutexattr(memory, 2, 2, 0, 2, 0);
    ASSERT(CALL(runtime, "mutex_init", M, MATTR) == ENOTSUP);
    assert_unused(runtime);
    OK(ios27_21i_destroy(runtime));

    OK(ios27_21i_create_apple_lp64_attrs(&runtime, &guest, 4));
    ASSERT(CALL(runtime, "mutex_init", M + 1u, MATTR) == EINVAL);
    ASSERT(CALL(runtime, "mutex_init", M, MATTR + 1u) == EINVAL);
    ASSERT(CALL(runtime, "mutex_init", M, BASE + 2040u) == EFAULT);
    ASSERT(CALL(runtime, "mutex_init", M, M) == EINVAL);
    le64(memory + 512, 0x1234u);
    ASSERT(CALL(runtime, "mutex_init", M, MATTR) == EINVAL);
    assert_unused(runtime);
    set_mutexattr(memory, 2, 1, 0, 2, 0);
    ASSERT(CALL(runtime, "mutex_init", M, MATTR) == ENOTSUP);
    set_mutexattr(memory, 2, 2, 1, 2, 0);
    ASSERT(CALL(runtime, "mutex_init", M, MATTR) == ENOTSUP);
    set_mutexattr(memory, 2, 2, 0, 0, 0);
    ASSERT(CALL(runtime, "mutex_init", M, MATTR) == ENOTSUP);
    set_mutexattr(memory, 2, 2, 0, 2, 1);
    ASSERT(CALL(runtime, "mutex_init", M, MATTR) == ENOTSUP);
    set_mutexattr(memory, 3, 2, 0, 2, 0);
    ASSERT(CALL(runtime, "mutex_init", M, MATTR) == EINVAL);
    assert_unused(runtime);
    /* Recursive mutex from Apple-published LP64 attr bit layout. */
    set_mutexattr(memory, 2, 2, 0, 2, 0);
    OK(CALL(runtime, "mutex_init", M, MATTR));
    ASSERT(CALL(runtime, "mutexattr_init", M, 0) == EBUSY);
    OK(CALL(runtime, "mutex_lock", M, 0));
    OK(CALL(runtime, "mutex_lock", M, 0));
    OK(CALL(runtime, "mutex_trylock", M, 0));
    ASSERT(CALL(runtime, "mutex_destroy", M, 0) == EBUSY);
    OK(CALL(runtime, "mutex_unlock", M, 0));
    OK(CALL(runtime, "mutex_unlock", M, 0));
    OK(CALL(runtime, "mutex_unlock", M, 0));
    OK(CALL(runtime, "mutex_destroy", M, 0));
    /* Error-checking mutex and normal mode remain distinct. */
    set_mutexattr(memory, 1, 2, 0, 1, 0);
    OK(CALL(runtime, "mutex_init", M, MATTR));
    OK(CALL(runtime, "mutex_lock", M, 0));
    ASSERT(CALL(runtime, "mutex_lock", M, 0) == EDEADLK);
    ASSERT(CALL(runtime, "mutex_trylock", M, 0) == EBUSY);
    OK(CALL(runtime, "mutex_unlock", M, 0));
    OK(CALL(runtime, "mutex_destroy", M, 0));
    set_mutexattr(memory, 0, 2, 0, 2, 0);
    OK(CALL(runtime, "mutex_init", M, MATTR));
    OK(CALL(runtime, "mutex_lock", M, 0));
    ASSERT(CALL(runtime, "mutex_trylock", M, 0) == EBUSY);
    OK(CALL(runtime, "mutex_unlock", M, 0));
    OK(CALL(runtime, "mutex_destroy", M, 0));

    set_condattr(memory, 1);
    ASSERT(CALL(runtime, "cond_init", C, CATTR) == ENOTSUP);
    set_condattr(memory, 2);
    le64(memory + 640, 0x9999u);
    ASSERT(CALL(runtime, "cond_init", C, CATTR) == EINVAL);
    set_condattr(memory, 2);
    ASSERT(CALL(runtime, "cond_init", C, C) == EINVAL);
    OK(CALL(runtime, "cond_init", C, CATTR));
    ASSERT(CALL(runtime, "condattr_init", C, 0) == EBUSY);
    OK(CALL(runtime, "cond_signal", C, 0));
    OK(CALL(runtime, "cond_broadcast", C, 0));
    OK(CALL(runtime, "cond_destroy", C, 0));
    /* Guest can construct and mutate published LP64 attributes through
     * actual exported pthread symbols, without Linux host pthread layouts. */
    OK(CALL(runtime, "mutexattr_init", MATTR, 0));
    ASSERT(CALL(runtime, "mutexattr_settype", MATTR, 3) == EINVAL);
    OK(CALL(runtime, "mutexattr_settype", MATTR,
            UINT64_C(0xffffffff00000002)));
    OK(CALL(runtime, "mutex_init", M, MATTR));
    OK(CALL(runtime, "mutex_lock", M, 0));
    OK(CALL(runtime, "mutex_lock", M, 0));
    OK(CALL(runtime, "mutex_unlock", M, 0));
    OK(CALL(runtime, "mutex_unlock", M, 0));
    OK(CALL(runtime, "mutex_destroy", M, 0));
    OK(CALL(runtime, "mutexattr_destroy", MATTR, 0));
    ASSERT(CALL(runtime, "mutex_init", M, MATTR) == EINVAL);
    ASSERT(CALL(runtime, "mutexattr_destroy", MATTR, 0) == EINVAL);
    OK(CALL(runtime, "condattr_init", CATTR, 0));
    OK(CALL(runtime, "cond_init", C, CATTR));
    OK(CALL(runtime, "cond_destroy", C, 0));
    OK(CALL(runtime, "condattr_destroy", CATTR, 0));
    ASSERT(CALL(runtime, "cond_init", C, CATTR) == EINVAL);
    assert_unused(runtime);
    OK(ios27_21i_destroy(runtime));
    puts("phase21i: Apple LP64 attr exported init/destroy/settype, signature/bitfield validation, recursive/errorcheck/normal, and rollback passed");
    return 0;
}
