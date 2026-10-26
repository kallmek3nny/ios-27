#ifndef IOS27_MACH_GUEST_H
#define IOS27_MACH_GUEST_H

#include "ios27/mach_v2.h"

#include <stddef.h>
#include <stdint.h>

#define IOS27_MACH_GUEST_SEND 1u
#define IOS27_MACH_GUEST_RECEIVE 2u

/*
 * Narrow offset-based synthetic Darwin-facing memory boundary.
 * 'guest' is a caller-owned, bounded userspace arena, NOT a raw guest
 * pointer or trusted host address. All offset+length arithmetic is
 * checked before invoking the v2 Mach-style queue.
 *
 * For RECEIVE, *actual is populated on success and TOO_LARGE, where
 * TOO_LARGE preserves the queued message and never modifies the arena.
 * For SEND, *actual is populated only on success. On any other error
 * the arena is unchanged. No OS traps, arbitrary syscalls, root or VM.
 */
int ios27_mach_guest_call(ios27_mach_v2 *space, uint32_t task,
                          uint32_t operation, uint32_t receive_name,
                          uint8_t *guest, size_t guest_size,
                          uint64_t guest_offset, uint64_t length,
                          uint32_t timeout_ms, uint32_t *actual);

#endif
