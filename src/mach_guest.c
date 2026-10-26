#include "ios27/mach_guest.h"

#include <string.h>

int ios27_mach_guest_call(ios27_mach_v2 *space, uint32_t task,
                          uint32_t operation, uint32_t receive_name,
                          uint8_t *guest, size_t guest_size,
                          uint64_t guest_offset, uint64_t length,
                          uint32_t timeout_ms, uint32_t *actual) {
    uint8_t staging[IOS27_MACH_MSG_MAX];
    size_t n;
    int result;
    if (!space || !guest || !actual ||
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
