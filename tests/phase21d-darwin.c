#include "ios27/libsystem21d_rw_once.h"

_Static_assert(sizeof(ios27_21d_handle) == 16, "bridge handle ABI size");
_Static_assert(_Alignof(ios27_21d_handle) == 8, "bridge handle ABI alignment");

/* Clang-produced ARM64 Darwin Mach-O object, real C callsites and symbols.
   This is a bridge ABI fixture, not proof of linking against Apple's libSystem. */
static int initialize(void *ctx) {
    int *value = (int *)ctx;
    *value = 27;
    return 0;
}
int phase21d_guest_entry(ios27_21d_runtime *rt, int *value) {
    ios27_21d_handle rw, once;
    int err = ios27_21d_create(rt, IOS27_21D_RWLOCK, &rw);
    if (err) return err;
    err = ios27_21d_create(rt, IOS27_21D_ONCE, &once);
    if (err) { (void)ios27_21d_destroy(rt, rw); return err; }
    err = ios27_21d_once(rt, once, initialize, value);
    if (!err) err = ios27_21d_wrlock(rt, rw);
    if (!err) {
        ++*value;
        err = ios27_21d_unlock(rt, rw);
    }
    if (!err) err = ios27_21d_timedwrlock(rt, rw, INT64_MAX);
    if (!err) err = ios27_21d_unlock(rt, rw);
    if (!err) err = ios27_21d_timedrdlock(rt, rw, INT64_MAX);
    if (!err) err = ios27_21d_unlock(rt, rw);
    if (!err) err = ios27_21d_rdlock(rt, rw);
    if (!err) err = ios27_21d_unlock(rt, rw);
    (void)ios27_21d_destroy(rt, rw);
    (void)ios27_21d_destroy(rt, once);
    return err;
}
