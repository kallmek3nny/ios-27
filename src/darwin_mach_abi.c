#include "ios27/darwin_mach_abi.h"

#include <stdint.h>
#include <string.h>

/* Conservative half-open overlap check that rejects address overflow. */
static int overlap(const void *a, size_t asize,
                   const void *b, size_t bsize) {
    uintptr_t x = (uintptr_t)a;
    uintptr_t y = (uintptr_t)b;
    if (asize > UINTPTR_MAX - x || bsize > UINTPTR_MAX - y)
        return 1;
    return asize && bsize && x < y + bsize && y < x + asize;
}
static int checked32(uint64_t value) {
    return value <= UINT32_MAX;
}

int ios27_darwin_fixture_call(ios27_mach_v2 *space, uint32_t task,
                              uint32_t selector,
                              const ios27_darwin_guest_registers *input,
                              ios27_darwin_guest_registers *output,
                              uint8_t *arena, size_t arena_size) {
    ios27_darwin_guest_registers reply = {{0}};
    uint32_t a = 0u, b = 0u, bytes = 0u;
    unsigned receives = 0u;
    int status = IOS27_MACH_UNSUPPORTED;

    if (!space || !input || !output || !task ||
        (uintptr_t)input % _Alignof(ios27_darwin_guest_registers) != 0u ||
        (uintptr_t)output % _Alignof(ios27_darwin_guest_registers) != 0u ||
        task > IOS27_MACH_TASK_MAX ||
        (!arena && arena_size) ||
        overlap(input, sizeof(*input), output, sizeof(*output)) ||
        (arena && (overlap(input, sizeof(*input), arena, arena_size) ||
                   overlap(output, sizeof(*output), arena, arena_size))))
        return IOS27_MACH_BAD_ARGUMENT;

    switch (selector) {
        case IOS27_DARWIN_FIXTURE_ALLOCATE:
            status = ios27_mach_v2_allocate(space, task, &a);
            if (status == IOS27_MACH_OK) reply.x[0] = a;
            break;
        case IOS27_DARWIN_FIXTURE_MAKE_SEND:
            if (!checked32(input->x[0]) || !checked32(input->x[1]))
                return IOS27_MACH_BAD_ARGUMENT;
            status = ios27_mach_v2_make_send(space, task,
                                              (uint32_t)input->x[0],
                                              (uint32_t)input->x[1], &a);
            if (status == IOS27_MACH_OK) reply.x[0] = a;
            break;
        case IOS27_DARWIN_FIXTURE_DROP_SEND:
        case IOS27_DARWIN_FIXTURE_DROP_ONCE:
        case IOS27_DARWIN_FIXTURE_DESTROY:
        case IOS27_DARWIN_FIXTURE_RIGHTS:
            if (!checked32(input->x[0])) return IOS27_MACH_BAD_ARGUMENT;
            if (selector == IOS27_DARWIN_FIXTURE_DROP_SEND)
                status = ios27_mach_v2_drop_send(space, task,
                                                 (uint32_t)input->x[0]);
            else if (selector == IOS27_DARWIN_FIXTURE_DROP_ONCE)
                status = ios27_mach_v2_drop_send_once(
                    space, task, (uint32_t)input->x[0]);
            else if (selector == IOS27_DARWIN_FIXTURE_DESTROY)
                status = ios27_mach_v2_destroy(space, task,
                                                (uint32_t)input->x[0]);
            else {
                status = ios27_mach_v2_rights(space, task,
                                             (uint32_t)input->x[0],
                                             &a, &b, &receives);
                if (status == IOS27_MACH_OK) {
                    reply.x[0] = a;
                    reply.x[1] = b;
                    reply.x[2] = receives;
                }
            }
            break;
        case IOS27_DARWIN_FIXTURE_WATCH_DEAD:
            if (!checked32(input->x[0])) return IOS27_MACH_BAD_ARGUMENT;
            status = ios27_mach_v2_watch_dead_name(space, task,
                                                    (uint32_t)input->x[0]);
            break;
        case IOS27_DARWIN_FIXTURE_POLL_DEAD:
            status = ios27_mach_v2_poll_dead_name(space, task, &a);
            if (status == IOS27_MACH_OK) reply.x[0] = a;
            break;
        case IOS27_DARWIN_FIXTURE_TEARDOWN:
            status = ios27_mach_v2_task_teardown(space, task);
            break;
        case IOS27_DARWIN_FIXTURE_SEND:
            if (!checked32(input->x[2])) return IOS27_MACH_BAD_ARGUMENT;
            status = ios27_mach_guest_call(space, task, IOS27_MACH_GUEST_SEND,
                                           0u, arena, arena_size,
                                           input->x[0], input->x[1],
                                           (uint32_t)input->x[2], &bytes);
            if (status == IOS27_MACH_OK) reply.x[0] = bytes;
            break;
        case IOS27_DARWIN_FIXTURE_RECEIVE:
            if (!checked32(input->x[0]) || !checked32(input->x[3]))
                return IOS27_MACH_BAD_ARGUMENT;
            status = ios27_mach_guest_call(space, task,
                                           IOS27_MACH_GUEST_RECEIVE,
                                           (uint32_t)input->x[0],
                                           arena, arena_size, input->x[1],
                                           input->x[2],
                                           (uint32_t)input->x[3], &bytes);
            if (status == IOS27_MACH_OK ||
                status == IOS27_MACH_TOO_LARGE)
                reply.x[0] = bytes;
            break;
        default:
            return IOS27_MACH_UNSUPPORTED;
    }
    if (status == IOS27_MACH_OK || status == IOS27_MACH_TOO_LARGE)
        *output = reply;
    return status;
}
