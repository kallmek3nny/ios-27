#include "ios27/libsystem21h_lifecycle.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
static atomic_int inject_failure = ATOMIC_VAR_INIT(1);
/* Linked from a source-only macro override; this call uses the real libc API. */
int ios27_21h_test_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
    if (atomic_exchange(&inject_failure, 0)) return ENOMEM;
    return pthread_cond_init(cond, attr);
}
#define CHECK(x) do { if (!(x)) abort(); } while (0)
int main(void) {
    ios27_21h_runtime *rt = NULL;
    ios27_21h_handle h;
    ios27_21h_stats st;
    CHECK(ios27_21h_runtime_create(&rt, 1) == 0);
    CHECK(ios27_21h_object_create(rt, &h) == ENOMEM);
    CHECK(h.slot == 0 && h.generation == 0);
    CHECK(ios27_21h_get_stats(rt, &st) == 0);
    CHECK(st.live == 0 && st.waiting == 0 && st.owned == 0);
    CHECK(ios27_21h_object_create(rt, &h) == 0);
    CHECK(h.slot == 1 && h.generation == 1);
    CHECK(ios27_21h_object_destroy(rt, h) == 0);
    CHECK(ios27_21h_runtime_destroy(rt) == 0);
    puts("phase21h-fault: failed initialization rolled back without publishing a handle");
    return 0;
}
