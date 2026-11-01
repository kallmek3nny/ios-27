#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21f_mutex.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr,"phase21f capacity %s:%d: %s\n",__FILE__,__LINE__,#expr); \
    abort(); } } while (0)
int main(void) {
    enum { ROUNDS = 100 };
    ios27_21f_runtime *rt = NULL;
    ios27_21f_handle active[IOS27_21F_MAX_MUTEXES];
    ios27_21f_handle stale[IOS27_21F_MAX_MUTEXES];
    ios27_21f_handle overflow = {1u, 1u, 1u};
    CHECK(ios27_21f_runtime_create(&rt, IOS27_21F_MAX_MUTEXES) == 0);
    for (unsigned round = 0; round < ROUNDS; ++round) {
        for (unsigned i = 0; i < IOS27_21F_MAX_MUTEXES; ++i) {
            ios27_21f_kind kind = (ios27_21f_kind)(IOS27_21F_NORMAL + (i % 3));
            CHECK(ios27_21f_mutex_create(rt, kind, &active[i]) == 0);
            CHECK(active[i].runtime_id != 0);
        }
        CHECK(ios27_21f_mutex_create(rt, IOS27_21F_NORMAL, &overflow) == EAGAIN);
        CHECK(overflow.slot == 0 && overflow.generation == 0 && overflow.runtime_id == 0);
        for (unsigned i = 0; i < IOS27_21F_MAX_MUTEXES; ++i) {
            CHECK(ios27_21f_mutex_lock(rt, active[i]) == 0);
            CHECK(ios27_21f_mutex_trylock(rt, active[i]) ==
                  ((i % 3) == 1 ? 0 : EBUSY));
            if ((i % 3) == 1) CHECK(ios27_21f_mutex_unlock(rt, active[i]) == 0);
            CHECK(ios27_21f_mutex_unlock(rt, active[i]) == 0);
            stale[i] = active[i];
            CHECK(ios27_21f_mutex_destroy(rt, active[i]) == 0);
            CHECK(ios27_21f_mutex_lock(rt, stale[i]) == EINVAL);
        }
    }
    CHECK(ios27_21f_runtime_destroy(rt) == 0);
    printf("phase21f capacity: %u full-pool cycles, %u mutex generations passed\n",
           ROUNDS, (unsigned)(ROUNDS * IOS27_21F_MAX_MUTEXES));
    return 0;
}
