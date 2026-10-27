#include "ios27/mach_guest.h"

#include <stdint.h>
#include <string.h>

int ios27_mach_guest_call(ios27_mach_v2 *space, uint32_t task,
                          uint32_t operation, uint32_t receive_name,
                          uint8_t *guest, size_t guest_size,
                          uint64_t guest_offset, uint64_t length,
                          uint32_t timeout_ms, uint32_t *actual) {
    uint8_t staging[IOS27_MACH_MSG_MAX];
    size_t n;
    uintptr_t arena_start, result_start;
    int result;
    if (!space || !guest || !actual) return IOS27_MACH_BAD_ARGUMENT;
    arena_start = (uintptr_t)guest;
    result_start = (uintptr_t)actual;
    /* A result pointer within the arena could violate failure atomicity. */
    if (guest_size > UINTPTR_MAX - arena_start ||
        result_start > UINTPTR_MAX - sizeof(*actual) ||
        (result_start < arena_start + guest_size &&
         arena_start < result_start + sizeof(*actual)))
        return IOS27_MACH_BAD_ARGUMENT;
    if (
        guest_offset > (uint64_t)guest_size ||
        length > (uint64_t)guest_size - guest_offset ||
        length > IOS27_MACH_MSG_MAX ||
        (operation != IOS27_MACH_GUEST_SEND &&
         operation != IOS27_MACH_GUEST_RECEIVE))
        return IOS27_MACH_BAD_ARGUMENT;
    n = (size_t)length;
    if (operation == IOS27_MACH_GUEST_SEND) {
        if (receive_name != 0u || n < sizeof(ios27_mach_header))
            return IOS27_MACH_BAD_ARGUMENT;
        /* Even a malformed guest wire never aliases host queue bytes. */
        memcpy(staging, guest + (size_t)guest_offset, n);
        result = ios27_mach_v2_send(space, task, staging, n, timeout_ms);
        if (result == IOS27_MACH_OK) *actual = (uint32_t)n;
        return result;
    }
    /* Preflighted, private temporary receive; commit only after success. */
    result = ios27_mach_v2_receive(space, task, receive_name,
                                   staging, &n, timeout_ms);
    if (result == IOS27_MACH_TOO_LARGE) {
        *actual = (uint32_t)n;
        return result;
    }
    if (result != IOS27_MACH_OK) return result;
    if (n > length) return IOS27_MACH_BAD_ARGUMENT; /* defensive */
    memcpy(guest + (size_t)guest_offset, staging, n);
    *actual = (uint32_t)n;
    return IOS27_MACH_OK;
}


/*
 * Keep the length result inside the same validated guest-virtual arena.
 * A disjoint four-byte slot is reserved before the Mach fixture is called,
 * so a successful dequeue can never be followed by a failed guest write.
 */
int ios27_mach_guest_call_offset(ios27_mach_v2 *space, uint32_t task,
                                 uint32_t operation, uint32_t receive_name,
                                 uint8_t *guest, size_t guest_size,
                                 uint64_t guest_offset, uint64_t length,
                                 uint32_t timeout_ms, uint64_t actual_offset) {
    uint32_t actual = 0u;
    int result;
    if (!guest || actual_offset > (uint64_t)guest_size ||
        (uint64_t)guest_size - actual_offset < sizeof(actual) ||
        guest_offset > (uint64_t)guest_size ||
        length > (uint64_t)guest_size - guest_offset ||
        length > IOS27_MACH_MSG_MAX)
        return IOS27_MACH_BAD_ARGUMENT;
    /* No guest result store may overwrite the wire input or output span. */
    if (guest_offset < actual_offset + sizeof(actual) &&
        actual_offset < guest_offset + length)
        return IOS27_MACH_BAD_ARGUMENT;

    result = ios27_mach_guest_call(space, task, operation, receive_name,
                                   guest, guest_size, guest_offset, length,
                                   timeout_ms, &actual);
    if (result == IOS27_MACH_OK || result == IOS27_MACH_TOO_LARGE) {
        for (unsigned i = 0u; i < sizeof(actual); ++i)
            guest[(size_t)actual_offset + i] = (uint8_t)(actual >> (8u * i));
    }
    return result;
}
