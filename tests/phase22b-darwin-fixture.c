#include "ios27/phase22b_syscalls.h"
/* Cross-compiled genuine ARM64 Darwin Mach-O object, SDK-free. It calls the
   same dispatcher under test. It is NOT a native kernel syscall or an Apple
   libSystem entrypoint and cannot be executed directly on Linux. */
int64_t ios27_22b_darwin_style_caller(ios27_22b_context *c,
                                     uint64_t guest_offset, uint64_t count) {
    uint64_t a[6]={1,guest_offset,count,0,0,0};
    ios27_22b_result r=ios27_22b_dispatch(c,IOS27_22B_BSD|IOS27_22B_WRITE,a);
    return r.carry ? -(int64_t)r.darwin_errno : r.x0;
}