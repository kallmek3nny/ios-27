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
#define IOS27_DARWIN_FIXTURE_SET_ALLOCATE UINT32_C(0x4953010c)
#define IOS27_DARWIN_FIXTURE_SET_ADD      UINT32_C(0x4953010d)
#define IOS27_DARWIN_FIXTURE_SET_REMOVE   UINT32_C(0x4953010e)
#define IOS27_DARWIN_FIXTURE_SET_DESTROY  UINT32_C(0x4953010f)
#define IOS27_DARWIN_FIXTURE_RECEIVE_SET  UINT32_C(0x49530110)
#define IOS27_DARWIN_FIXTURE_RECEIVE_TRAILER UINT32_C(0x49530111)
#define IOS27_DARWIN_FIXTURE_RECEIVE_OOL UINT32_C(0x49530112)
#define IOS27_DARWIN_FIXTURE_THREAD_CREATE UINT32_C(0x49530113)
#define IOS27_DARWIN_FIXTURE_THREAD_DESTROY UINT32_C(0x49530114)
#define IOS27_DARWIN_FIXTURE_THREAD_IDENTITY UINT32_C(0x49530115)
#define IOS27_DARWIN_FIXTURE_EXCEPTION_SET UINT32_C(0x49530116)
#define IOS27_DARWIN_FIXTURE_EXCEPTION_GET UINT32_C(0x49530117)
#define IOS27_DARWIN_FIXTURE_EXCEPTION_RAISE UINT32_C(0x49530118)
#define IOS27_DARWIN_FIXTURE_VM_COPY UINT32_C(0x49530119)
#define IOS27_DARWIN_FIXTURE_VM_ZERO UINT32_C(0x4953011a)
#define IOS27_DARWIN_FIXTURE_VM_MAX 4096u

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
 *   SET_ALLOCATE: no args -> out.x0 = set name
 *   SET_ADD/SET_REMOVE: x0 set, x1 owned receive name
 *   SET_DESTROY: x0 set
 *   RECEIVE_SET: x0 set, x1 arena offset, x2 capacity, x3 timeout
 *   RECEIVE_TRAILER: x0 receive name, x1 offset, x2 capacity, x3 timeout
 *   RECEIVE_OOL: x0 receive name, x1 offset, x2 wire+OOL capacity,
 *                x3 timeout. Returned OOL address is a guest-arena offset.
 *
 * THREAD_CREATE -> out.x0 = global non-recycled synthetic thread ID
 * THREAD_DESTROY / THREAD_IDENTITY: x0 thread ID (IDENTITY -> out.x0 owner)
 * EXCEPTION_SET: x0 bit mask (bits 0..7), x1 task-local send right (or 0)
 * EXCEPTION_GET: x0 exception bit index -> out.x0 active send name or 0
 * EXCEPTION_RAISE: x0 thread ID, x1 exception bit, x2 exception code
 * VM_COPY: x0 source offset, x1 destination offset, x2 byte count
 * VM_ZERO: x0 destination offset, x1 byte count
 * VM_COPY/VM_ZERO operate only within the supplied disjoint guest arena and
 * have a 4KiB per-call budget. They are not host mmap/mprotect or XNU traps.
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
