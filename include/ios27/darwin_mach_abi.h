#ifndef IOS27_DARWIN_MACH_ABI_H
#define IOS27_DARWIN_MACH_ABI_H

#include "ios27/mach_guest.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Phase 11 narrow *synthetic* Darwin-facing AArch64 register boundary.
 * Selectors are private fixture IDs, NOT Apple/XNU Mach trap numbers.
 * A caller supplies an authenticated-by-the-test task ID and a bounded
 * guest byte arena. There are no syscalls, pointers from guest registers,
 * process credentials, kernel services, or platform hooks.
 */
#define IOS27_DARWIN_FIXTURE_ALLOCATE   UINT32_C(0x49530101)
#define IOS27_DARWIN_FIXTURE_MAKE_SEND  UINT32_C(0x49530102)
#define IOS27_DARWIN_FIXTURE_DROP_SEND  UINT32_C(0x49530103)
#define IOS27_DARWIN_FIXTURE_DROP_ONCE  UINT32_C(0x49530104)
#define IOS27_DARWIN_FIXTURE_DESTROY    UINT32_C(0x49530105)
#define IOS27_DARWIN_FIXTURE_RIGHTS     UINT32_C(0x49530106)
#define IOS27_DARWIN_FIXTURE_SEND       UINT32_C(0x49530107)
#define IOS27_DARWIN_FIXTURE_RECEIVE    UINT32_C(0x49530108)
#define IOS27_DARWIN_FIXTURE_TEARDOWN   UINT32_C(0x49530109)
#define IOS27_DARWIN_FIXTURE_WATCH_DEAD UINT32_C(0x4953010a)
#define IOS27_DARWIN_FIXTURE_POLL_DEAD  UINT32_C(0x4953010b)

typedef struct {
    uint64_t x[8];
} ios27_darwin_guest_registers;

/*
 * Inputs:
 *   ALLOCATE: no arguments                   -> out.x[0] = local port
 *   MAKE_SEND: x0 receive name, x1 recipient -> out.x[0] = recipient name
 *   DROP_SEND/DROP_ONCE/DESTROY: x0 port
 *   RIGHTS: x0 port -> out.x0 sends, x1 send-once, x2 receive
 *   SEND: x0 arena offset, x1 length, x2 timeout
 *   RECEIVE: x0 local port, x1 offset, x2 capacity, x3 timeout
 *   TEARDOWN/POLL_DEAD: no arguments (POLL_DEAD -> out.x0 local name)
 *   WATCH_DEAD: x0 existing send/send-once name
 *
 * SEND/RECEIVE success: out.x[0] = transferred bytes.
 * RECEIVE TOO_LARGE: out.x[0] = required bytes; the message remains queued.
 * On other errors output registers and guest arena remain unmodified.
 * The caller must keep the arena, input and output registers disjoint.
 */
int ios27_darwin_fixture_call(ios27_mach_v2 *space, uint32_t task,
                              uint32_t selector,
                              const ios27_darwin_guest_registers *input,
                              ios27_darwin_guest_registers *output,
                              uint8_t *arena, size_t arena_size);

#endif
