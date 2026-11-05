#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21i_abi.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21i capacity failure at %s:%d: %s\\n", \
            __FILE__, __LINE__, #x); abort(); \
} } while (0)
#define OK(x) CHECK((x) == 0)
#define BASE UINT64_C(0x600000000)
#define ADDR(i) (BASE + 64u + (uint64_t)(i) * 128u)
#define DISPATCH(rt, op, addr) ios27_21i_dispatch(rt, "_pthread_mutex_" op, addr, 0, 0)
enum { SLOTS = 128 };
int main(void) {
    uint8_t memory[SLOTS * 130u] = {0};
    ios27_21i_guest guest = {memory, sizeof(memory), BASE, 64, 48, 8, 8};
    ios27_21i_runtime *runtime = NULL;
    ios27_21i_token first[SLOTS], second[SLOTS];
    ios27_21i_stats stats;
    runtime = (ios27_21i_runtime *)(uintptr_t)1;
    CHECK(ios27_21i_create(&runtime, &guest, SLOTS + 1u) == EINVAL);
    CHECK(runtime == NULL);
    OK(ios27_21i_create(&runtime, &guest, SLOTS));
    for (unsigned i = 0; i < SLOTS; ++i) {
        OK(DISPATCH(runtime, "init", ADDR(i)));
        OK(ios27_21i_lookup(runtime, ADDR(i), IOS27_21I_MUTEX, &first[i]));
        CHECK(first[i].generation != 0);
    }
    OK(ios27_21i_get_stats(runtime, &stats));
    CHECK(stats.capacity == SLOTS && stats.mutexes == SLOTS &&
          !stats.conditions && !stats.in_flight && !stats.retired_slots);
    CHECK(DISPATCH(runtime, "init", ADDR(SLOTS)) == EAGAIN);
    CHECK(DISPATCH(runtime, "init", ADDR(0)) == EBUSY);
    CHECK(ios27_21i_destroy(runtime) == EBUSY);
    for (unsigned i = SLOTS; i-- > 0;) {
        OK(DISPATCH(runtime, "destroy", ADDR(i)));
        CHECK(ios27_21i_verify(runtime, ADDR(i), IOS27_21I_MUTEX,
                                first[i]) == EINVAL);
    }
    OK(ios27_21i_get_stats(runtime, &stats));
    CHECK(!stats.mutexes && !stats.conditions && !stats.in_flight);
    for (unsigned i = 0; i < SLOTS; ++i) {
        OK(DISPATCH(runtime, "init", ADDR(i)));
        OK(ios27_21i_lookup(runtime, ADDR(i), IOS27_21I_MUTEX, &second[i]));
        CHECK(second[i].generation > first[i].generation);
        CHECK(ios27_21i_verify(runtime, ADDR(i), IOS27_21I_MUTEX,
                                first[i]) == EINVAL);
        OK(DISPATCH(runtime, "lock", ADDR(i)));
        OK(DISPATCH(runtime, "unlock", ADDR(i)));
    }
    for (unsigned i = 0; i < SLOTS; ++i)
        OK(DISPATCH(runtime, "destroy", ADDR(i)));
    OK(ios27_21i_get_stats(runtime, &stats));
    CHECK(stats.capacity == SLOTS && !stats.mutexes &&
          !stats.conditions && !stats.in_flight && !stats.retired_slots);
    OK(ios27_21i_destroy(runtime));
    puts("phase21i: all 128 slots exhausted and twice recycled, generation transitions, no object/reference leaks");
    return 0;
}
