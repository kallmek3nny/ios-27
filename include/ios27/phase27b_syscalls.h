#ifndef IOS27_PHASE27B_SYSCALLS_H
#define IOS27_PHASE27B_SYSCALLS_H
#include "ios27/libsystem20e_services.h"
#include <stdint.h>

/* Same v1 service bridge; only these additional Darwin BSD selectors are
 * recognized. All addresses are private 20e arena offsets, never host paths. */
int64_t ios27_27b_syscall(ios27_20e_ctx *ctx, uint32_t selector,
                          uint64_t a0, uint64_t a1, uint64_t a2,
                          uint64_t a3, uint64_t a4, uint64_t a5);
#endif
