/* SDK-free Clang-produced ARM64 Darwin Mach-O relocation callsites. */
#include "ios27/libsystem21c_sync.h"
struct timespec { long tv_sec; long tv_nsec; };
__attribute__((visibility("default")))
int ios27_phase21c_darwin_calls(ios27_21c_runtime *rt,
                               ios27_21c_handle m,
                               ios27_21c_handle c) {
    struct timespec deadline = {1000, 1234};
    int rc = ios27_21c_mutex_lock(rt, m);
    if (rc) return rc;
    rc = ios27_21c_cond_timedwait(rt, c, m, IOS27_21C_REALTIME, &deadline);
    if (!rc) rc = ios27_21c_cond_broadcast(rt, c);
    if (ios27_21c_mutex_unlock(rt, m)) return -1;
    return rc;
}
