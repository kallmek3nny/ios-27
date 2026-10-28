#include "ios27/libsystem21i_arm64.h"
#include "ios27/libsystem21i_errno.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPECT(x) do { if (!(x)) { \
    fprintf(stderr, "phase21i arm64 register test %s:%d: %s\\n", \
            __FILE__, __LINE__, #x); abort(); \
} } while (0)
#define BASE UINT64_C(0x200000000)

static int invoke(ios27_21i_runtime *rt, const char *symbol,
                  uint64_t x0, uint64_t x1, uint64_t x2) {
    ios27_21i_arm64_registers regs = {{
        x0, x1, x2, UINT64_C(0xdedabc001),
        UINT64_C(0xdedabc002), UINT64_C(0xdedabc003),
        UINT64_C(0xdedabc004), UINT64_C(0xdedabc005)
    }};
    EXPECT(ios27_21i_dispatch_arm64(rt, symbol, &regs) == 0);
    EXPECT(regs.x[1] == x1 && regs.x[2] == x2);
    EXPECT(regs.x[3] == UINT64_C(0xdedabc001));
    EXPECT(regs.x[4] == UINT64_C(0xdedabc002));
    EXPECT(regs.x[5] == UINT64_C(0xdedabc003));
    EXPECT(regs.x[6] == UINT64_C(0xdedabc004));
    EXPECT(regs.x[7] == UINT64_C(0xdedabc005));
    EXPECT(regs.x[0] <= UINT32_MAX);
    return (int)(uint32_t)regs.x[0];
}

int main(void) {
    uint8_t memory[512];
    uint8_t original[sizeof(memory)];
    ios27_21i_guest guest = {memory, sizeof(memory), BASE, 64, 48, 8, 8};
    ios27_21i_runtime *rt = NULL;
    ios27_21i_arm64_registers regs = {{
        UINT64_C(0x1122334455667788), UINT64_C(0x8877665544332211)
    }};
    memset(memory, 0xa5, sizeof(memory));
    memset(memory + 384, 0, 16); /* a valid absolute deadline in the past */
    memcpy(original, memory, sizeof(original));
    EXPECT(ios27_21i_errno_to_darwin(EDEADLK) == 11);
    EXPECT(ios27_21i_errno_to_darwin(EAGAIN) == 35);
    EXPECT(ios27_21i_errno_to_darwin(ENOTSUP) == 45);
    EXPECT(ios27_21i_errno_to_darwin(ETIMEDOUT) == 60);
    EXPECT(ios27_21i_errno_to_darwin(ENOSYS) == 78);
    EXPECT(ios27_21i_errno_to_darwin(100000) == 5);
    EXPECT(ios27_21i_create(&rt, &guest, 3) == 0);
    EXPECT(ios27_21i_dispatch_arm64(NULL, "_pthread_mutex_lock", &regs)
           == EINVAL);
    EXPECT(regs.x[0] == UINT64_C(0x1122334455667788));
    EXPECT(ios27_21i_dispatch_arm64(rt, NULL, &regs) == EINVAL);
    EXPECT(ios27_21i_dispatch_arm64(rt, "_pthread_mutex_lock", NULL)
           == EINVAL);
    EXPECT(invoke(rt, "_pthread_mutex_init", BASE + 64, BASE + 256, 0)
           == IOS27_21I_DARWIN_ENOTSUP);
    EXPECT(invoke(rt, "_pthread_mutex_init", BASE + 65, 0, 0) == IOS27_21I_DARWIN_EINVAL);
    EXPECT(invoke(rt, "_pthread_mutex_init", BASE + 64, 0, 0) == 0);
    EXPECT(invoke(rt, "_pthread_mutex_lock", BASE + 64, 0, 0) == 0);
    EXPECT(invoke(rt, "_pthread_mutex_trylock", BASE + 64, 0, 0) == IOS27_21I_DARWIN_EBUSY);
    EXPECT(invoke(rt, "_pthread_mutex_lock", BASE + 64, 0, 0) == IOS27_21I_DARWIN_EDEADLK);
    EXPECT(invoke(rt, "_pthread_mutex_destroy", BASE + 64, 0, 0) == IOS27_21I_DARWIN_EBUSY);
    EXPECT(invoke(rt, "_pthread_mutex_unlock", BASE + 64, 0, 0) == 0);
    EXPECT(invoke(rt, "_pthread_cond_init", BASE + 192, 0, 0) == 0);
    EXPECT(invoke(rt, "_pthread_cond_signal", BASE + 192, 0, 0) == 0);
    EXPECT(invoke(rt, "_pthread_cond_broadcast", BASE + 192, 0, 0) == 0);
    EXPECT(invoke(rt, "_pthread_mutex_lock", BASE + 64, 0, 0) == 0);
    EXPECT(invoke(rt, "_pthread_cond_timedwait", BASE + 192, BASE + 64,
                  BASE + 384) == IOS27_21I_DARWIN_ETIMEDOUT);
    EXPECT(invoke(rt, "_pthread_mutex_unlock", BASE + 64, 0, 0) == 0);
    EXPECT(invoke(rt, "_pthread_cond_timedwait", BASE + 192, BASE + 64,
                  BASE + 504) == IOS27_21I_DARWIN_EFAULT);
    EXPECT(invoke(rt, "_pthread_cond_destroy", BASE + 192, 0, 0) == 0);
    EXPECT(invoke(rt, "_pthread_mutex_destroy", BASE + 64, 0, 0) == 0);
    EXPECT(invoke(rt, "_pthread_nonexistent", BASE + 64, 0, 0) == IOS27_21I_DARWIN_ENOSYS);
    EXPECT(invoke(rt, "_pthread_mutex_lock", UINT64_MAX, 0, 0) == IOS27_21I_DARWIN_EINVAL);
    EXPECT(memcmp(memory, original, sizeof(memory)) == 0);
    EXPECT(ios27_21i_destroy(rt) == 0);
    puts("phase21i: AArch64 argument decoding, Apple XNU errno in w0, preserved x1-x7, unchanged guest layout passed");
    return 0;
}
