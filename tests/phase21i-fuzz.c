#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21i_abi.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BASE UINT64_C(0x300000000)
#define VERIFY(x) do { if (!(x)) { \
    fprintf(stderr, "phase21i fuzz %s:%d: %s\\n", \
            __FILE__, __LINE__, #x); abort(); \
} } while (0)
enum { ROUNDS = 60000, OBJECT_ADDRS = 8 };

static uint64_t next(uint64_t *seed) {
    uint64_t s = *seed;
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    *seed = s;
    return s;
}
static int acceptable(int rc) {
    switch (rc) {
    case 0: case EINVAL: case EFAULT: case ENOTSUP: case EBUSY:
    case EAGAIN: case EPERM: case EDEADLK: case ETIMEDOUT: case ENOSYS:
        return 1;
    default: return 0;
    }
}
int main(void) {
    static const char *const symbols[] = {
        "_pthread_mutex_init", "_pthread_cond_init",
        "_pthread_mutex_destroy", "_pthread_cond_destroy",
        "_pthread_mutex_lock", "_pthread_mutex_trylock",
        "_pthread_mutex_unlock", "_pthread_cond_signal",
        "_pthread_cond_broadcast", "_pthread_cond_timedwait",
        "_pthread_not_a_real_symbol"
    };
    static const uint64_t invalid[] = {
        0, 1, BASE - 8, BASE + 1, BASE + 3, BASE + 4090,
        BASE + 4088, UINT64_MAX, UINT64_MAX - 7u
    };
    uint8_t guest_memory[4096] = {0};
    uint64_t locations[OBJECT_ADDRS];
    ios27_21i_guest guest = {guest_memory, sizeof(guest_memory),
                             BASE, 64, 48, 8, 8};
    ios27_21i_runtime *runtime = NULL;
    ios27_21i_stats stats;
    uint64_t seed = UINT64_C(0x21a11baadc0ffee1);
    unsigned failures = 0;
    VERIFY(ios27_21i_create(&runtime, &guest, OBJECT_ADDRS) == 0);
    for (unsigned i = 0; i < OBJECT_ADDRS; ++i)
        locations[i] = BASE + 64u + i * 128u;

    for (unsigned n = 0; n < ROUNDS; ++n) {
        uint64_t choice = next(&seed);
        uint64_t address = choice & 3u
            ? locations[(choice >> 3) % OBJECT_ADDRS]
            : invalid[(choice >> 3) % (sizeof(invalid)/sizeof(invalid[0]))];
        const char *symbol = symbols[(choice >> 12) %
                                      (sizeof(symbols)/sizeof(symbols[0]))];
        uint64_t second = locations[(choice >> 20) % OBJECT_ADDRS];
        uint64_t third = BASE + 3808u;
        if (((choice >> 29) & 3u) == 0)
            second = invalid[(choice >> 33) %
                             (sizeof(invalid)/sizeof(invalid[0]))];
        if (((choice >> 37) & 3u) == 0)
            third = invalid[(choice >> 41) %
                            (sizeof(invalid)/sizeof(invalid[0]))];
        /* Attribute pointers for init default to NULL and sometimes fail
         * with ENOTSUP; timestamps always point to initialized guest bytes. */
        if (!strcmp(symbol, "_pthread_mutex_init") ||
            !strcmp(symbol, "_pthread_cond_init"))
            second = ((choice >> 17) & 7u) ? 0 : second;
        int rc = ios27_21i_dispatch(runtime, symbol, address,
                                    second, third);
        VERIFY(acceptable(rc));
        if (rc) ++failures;
        if ((n & 1023u) == 0) {
            VERIFY(ios27_21i_get_stats(runtime, &stats) == 0);
            VERIFY(stats.mutexes + stats.conditions <= OBJECT_ADDRS);
            VERIFY(stats.in_flight == 0);
            VERIFY(stats.retired_slots == 0);
        }
    }
    /* The single fuzzer thread is the only possible mutex owner. */
    for (unsigned i = 0; i < OBJECT_ADDRS; ++i) {
        const uint64_t addr = locations[i];
        int rc = ios27_21i_dispatch(runtime, "_pthread_mutex_unlock",
                                     addr, 0, 0);
        VERIFY(rc == 0 || rc == EPERM || rc == EINVAL);
        rc = ios27_21i_dispatch(runtime, "_pthread_mutex_destroy",
                                 addr, 0, 0);
        VERIFY(rc == 0 || rc == EINVAL);
        rc = ios27_21i_dispatch(runtime, "_pthread_cond_destroy",
                                 addr, 0, 0);
        VERIFY(rc == 0 || rc == EINVAL);
    }
    VERIFY(ios27_21i_get_stats(runtime, &stats) == 0);
    VERIFY(stats.mutexes == 0 && stats.conditions == 0 &&
           stats.in_flight == 0 && stats.retired_slots == 0);
    VERIFY(ios27_21i_destroy(runtime) == 0);
    for (size_t i = 0; i < sizeof(guest_memory); ++i)
        VERIFY(guest_memory[i] == 0);
    printf("phase21i: %u deterministic malformed/valid guest calls, %u rejected, zero leaked refs or guest layout writes\\n",
           (unsigned)ROUNDS, failures);
    return 0;
}
