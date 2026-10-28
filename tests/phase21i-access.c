#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21i_abi.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define ASSERT(x) do { if (!(x)) { \
    fprintf(stderr, "phase21i guest access %s:%d: %s\\n", \
            __FILE__, __LINE__, #x); abort(); } \
} while (0)
#define OK(x) ASSERT((x) == 0)
#define BASE UINT64_C(0x700000000)
#define M (BASE + 64u)
#define C (BASE + 192u)
#define C2 (BASE + 320u)
#define PROTECTED (BASE + 512u)
#define ATTR (BASE + 896u)
#define HOLE (BASE + 1024u)
#define DEADLINE (BASE + 768u)
#define CALL(rt, sym, a, b, c) ios27_21i_dispatch(rt, "_pthread_" sym, a, b, c)

typedef struct {
    size_t reads, writes, denials;
    int deny_mutex;
} access_state;
static int overlaps(uint64_t address, size_t length, uint64_t start,
                     uint64_t stop) {
    return address < stop && address + length > start;
}
static int checker(void *context, uint64_t address, size_t length,
                   int writable) {
    access_state *state = context;
    if (writable) ++state->writes;
    else ++state->reads;
    if (overlaps(address, length, HOLE, BASE + 2048u) ||
        (writable &&
         (overlaps(address, length, PROTECTED, BASE + 640u) ||
          (state->deny_mutex && overlaps(address, length, M, M + 64u))))) {
        ++state->denials;
        return 1;
    }
    return 0;
}
int main(void) {
    uint8_t memory[4096] = {0};
    ios27_21i_guest guest = {memory, sizeof(memory), BASE, 64, 48, 8, 8};
    ios27_21i_runtime *rt = NULL;
    ios27_21i_stats stats;
    access_state state = {0};
    ASSERT(ios27_21i_create_checked(&rt, &guest, 8, NULL, &state, 0) == EINVAL);
    ASSERT(rt == NULL);
    guest.mutex_extent = 48;
    ASSERT(ios27_21i_create_checked(&rt, &guest, 8,
                                    checker, &state, 1) == EINVAL);
    guest.mutex_extent = 64;
    OK(ios27_21i_create_checked(&rt, &guest, 8, checker, &state, 1));
    ASSERT(CALL(rt, "mutex_init", PROTECTED, 0, 0) == EFAULT);
    ASSERT(CALL(rt, "mutexattr_init", PROTECTED, 0, 0) == EFAULT);
    ASSERT(CALL(rt, "mutex_init", HOLE, 0, 0) == EFAULT);
    OK(CALL(rt, "mutex_init", M, 0, 0));
    OK(CALL(rt, "cond_init", C, 0, 0));
    OK(CALL(rt, "condattr_init", ATTR, 0, 0));
    OK(CALL(rt, "cond_init", C2, ATTR, 0));
    OK(CALL(rt, "mutex_lock", M, 0, 0));
    ASSERT(CALL(rt, "cond_timedwait", C, M, HOLE) == EFAULT);
    ASSERT(CALL(rt, "cond_timedwait", C, M, BASE + 4088u) == EFAULT);
    ASSERT(CALL(rt, "cond_timedwait", C, M, DEADLINE) == ETIMEDOUT);
    state.deny_mutex = 1;
    ASSERT(CALL(rt, "mutex_unlock", M, 0, 0) == EFAULT);
    ASSERT(CALL(rt, "mutex_destroy", M, 0, 0) == EFAULT);
    state.deny_mutex = 0;
    OK(CALL(rt, "mutex_unlock", M, 0, 0));
    OK(CALL(rt, "mutex_destroy", M, 0, 0));
    OK(CALL(rt, "cond_destroy", C, 0, 0));
    OK(CALL(rt, "cond_destroy", C2, 0, 0));
    OK(CALL(rt, "condattr_destroy", ATTR, 0, 0));
    OK(ios27_21i_get_stats(rt, &stats));
    ASSERT(stats.mutexes == 0 && stats.conditions == 0 &&
           stats.in_flight == 0 && stats.retired_slots == 0);
    OK(ios27_21i_destroy(rt));
    ASSERT(state.reads >= 3 && state.writes >= 12 && state.denials >= 5);
    puts("phase21i: denied read holes, read-only guest pages, safe timeout failure, protected object ownership and teardown passed");
    return 0;
}
