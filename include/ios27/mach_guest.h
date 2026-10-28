#ifndef IOS27_MACH_GUEST_H
#define IOS27_MACH_GUEST_H

#include "ios27/mach_v2.h"

#include <stddef.h>
#include <stdint.h>

#define IOS27_MACH_GUEST_SEND 1u
#define IOS27_MACH_GUEST_RECEIVE 2u
#define IOS27_MACH_GUEST_RECEIVE_SET 3u
#define IOS27_MACH_GUEST_RECEIVE_TRAILER 4u
#define IOS27_MACH_GUEST_RECEIVE_OOL 5u

/*
 * Narrow offset-based synthetic Darwin-facing memory boundary.
 * 'guest' is a caller-owned, bounded userspace arena, NOT a raw guest
 * pointer or trusted host address. All offset+length arithmetic is
 * checked before invoking the v2 Mach-style queue.
 *
 * For RECEIVE_SET, receive_name identifies the owner's port set. For
 * RECEIVE_TRAILER, it identifies a direct receive port and the caller's
 * wire span must fit the message and its 12-byte seqno trailer.
 * For RECEIVE_OOL, receive_name is a direct receive port. A single LP64
 * OOL memory descriptor may carry up to 128 bytes via explicit arena offsets.
 * The receiver's buffer must include room for wire + appended payload.
 * The returned descriptor address is rewritten to the payload's offset
 * in the receiver's arena. No host pointers or VM allocations are exposed.
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


/*
 * Guest-native variant: the message length/result is written as a uint32
 * in little-endian byte order to guest[actual_offset..actual_offset+4).
 * Both the wire span and result slot must fit the arena and not overlap.
 * On OK, commit wire bytes (RECEIVE) and the result slot; on TOO_LARGE,
 * leave the wire untouched and write only the required length; on any
 * other error, leave the entire guest arena unchanged.
 *
 * The result offset is guest-virtual: no arbitrary host result pointer
 * can be supplied through this entrypoint. No XNU syscalls are issued.
 */
int ios27_mach_guest_call_offset(ios27_mach_v2 *space, uint32_t task,
                                 uint32_t operation, uint32_t receive_name,
                                 uint8_t *guest, size_t guest_size,
                                 uint64_t guest_offset, uint64_t length,
                                 uint32_t timeout_ms, uint64_t actual_offset);

#endif
