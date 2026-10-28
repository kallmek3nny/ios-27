/* SDK-free, genuine Clang-generated ARM64 Darwin call sites. */
#include "ios27/libsystem21h_lifecycle.h"
int ios27_21h_darwin_caller(ios27_21h_runtime *rt) {
    ios27_21h_handle h = {0, 0};
    ios27_21h_stats stats;
    int rc = ios27_21h_object_create(rt, &h);
    if (rc) return rc;
    rc = ios27_21h_lock(rt, h);
    if (!rc) rc = ios27_21h_unlock(rt, h);
    if (!rc) rc = ios27_21h_object_close(rt, h);
    if (!rc) rc = ios27_21h_get_stats(rt, &stats);
    if (!rc) rc = ios27_21h_object_destroy(rt, h);
    return rc;
}
