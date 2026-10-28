#include "ios27/libsystem21h_lifecycle.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) abort(); } while (0)
int main(void) {
    ios27_21h_runtime *rt = NULL;
    ios27_21h_handle h, first;
    ios27_21h_stats st;
    CHECK(ios27_21h_runtime_create(&rt, 1) == 0);
    for (unsigned n = 1; n <= 3; ++n) {
        CHECK(ios27_21h_object_create(rt, &h) == 0);
        if (n == 1) first = h;
        CHECK(h.generation == n);
        CHECK(ios27_21h_object_destroy(rt, h) == 0);
        CHECK(ios27_21h_lock(rt, h) == EINVAL);
    }
    CHECK(ios27_21h_object_create(rt, &h) == EAGAIN);
    CHECK(h.slot == 0);
    CHECK(ios27_21h_lock(rt, first) == EINVAL);
    CHECK(ios27_21h_get_stats(rt, &st) == 0);
    CHECK(st.live == 0 && st.retired == 1);
    CHECK(ios27_21h_runtime_destroy(rt) == 0);
    puts("phase21h-rollover: exhausted generation cannot be reused");
    return 0;
}
