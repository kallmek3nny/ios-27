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
        ios27_mach_v2_validate_external(space, input, sizeof(*input)) !=
            IOS27_MACH_OK ||
        ios27_mach_v2_validate_external(space, output, sizeof(*output)) !=
            IOS27_MACH_OK ||
        ios27_mach_v2_validate_external(space, arena, arena_size) !=
            IOS27_MACH_OK ||
        overlap(input, sizeof(*input), output, sizeof(*output)) ||
        (arena && (overlap(input, sizeof(*input), arena, arena_size) ||
                   overlap(output, sizeof(*output), arena, arena_size))))
        return IOS27_MACH_BAD_ARGUMENT;

    switch (selector) {
        case IOS27_DARWIN_FIXTURE_ALLOCATE:
        case IOS27_DARWIN_FIXTURE_SET_ALLOCATE:
            status = (selector == IOS27_DARWIN_FIXTURE_ALLOCATE) ?
                     ios27_mach_v2_allocate(space, task, &a) :
                     ios27_mach_v2_set_allocate(space, task, &a);
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
        case IOS27_DARWIN_FIXTURE_SET_ADD:
        case IOS27_DARWIN_FIXTURE_SET_REMOVE:
            if (!checked32(input->x[0]) || !checked32(input->x[1]))
                return IOS27_MACH_BAD_ARGUMENT;
            status = (selector == IOS27_DARWIN_FIXTURE_SET_ADD) ?
                     ios27_mach_v2_set_add(space, task,
                                          (uint32_t)input->x[0],
                                          (uint32_t)input->x[1]) :
                     ios27_mach_v2_set_remove(space, task,
                                             (uint32_t)input->x[0],
                                             (uint32_t)input->x[1]);
            break;
        case IOS27_DARWIN_FIXTURE_DROP_SEND:
        case IOS27_DARWIN_FIXTURE_DROP_ONCE:
        case IOS27_DARWIN_FIXTURE_DESTROY:
        case IOS27_DARWIN_FIXTURE_SET_DESTROY:
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
            else if (selector == IOS27_DARWIN_FIXTURE_SET_DESTROY)
                status = ios27_mach_v2_set_destroy(space, task,
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
        case IOS27_DARWIN_FIXTURE_RECEIVE_SET:
        case IOS27_DARWIN_FIXTURE_RECEIVE_TRAILER:
        case IOS27_DARWIN_FIXTURE_RECEIVE_OOL:
            if (!checked32(input->x[0]) || !checked32(input->x[3]))
                return IOS27_MACH_BAD_ARGUMENT;
            status = ios27_mach_guest_call(space, task,
                selector == IOS27_DARWIN_FIXTURE_RECEIVE_SET ?
                IOS27_MACH_GUEST_RECEIVE_SET :
                selector == IOS27_DARWIN_FIXTURE_RECEIVE_TRAILER ?
                IOS27_MACH_GUEST_RECEIVE_TRAILER :
                selector == IOS27_DARWIN_FIXTURE_RECEIVE_OOL ?
                IOS27_MACH_GUEST_RECEIVE_OOL :
                IOS27_MACH_GUEST_RECEIVE,
                                           (uint32_t)input->x[0],
                                           arena, arena_size, input->x[1],
                                           input->x[2],
                                           (uint32_t)input->x[3], &bytes);
            if (status == IOS27_MACH_OK ||
                status == IOS27_MACH_TOO_LARGE)
                reply.x[0] = bytes;
            break;
        case IOS27_DARWIN_FIXTURE_THREAD_CREATE:
            status = ios27_mach_v2_thread_create(space, task, &a);
            if (status == IOS27_MACH_OK) reply.x[0] = a;
            break;
        case IOS27_DARWIN_FIXTURE_THREAD_DESTROY:
        case IOS27_DARWIN_FIXTURE_THREAD_IDENTITY:
            if (!checked32(input->x[0])) return IOS27_MACH_BAD_ARGUMENT;
            if (selector == IOS27_DARWIN_FIXTURE_THREAD_DESTROY)
                status = ios27_mach_v2_thread_destroy(space, task,
                                                       (uint32_t)input->x[0]);
            else {
                status = ios27_mach_v2_thread_identity(space, task,
                                    (uint32_t)input->x[0], &a);
                if (status == IOS27_MACH_OK) reply.x[0] = a;
            }
            break;
        case IOS27_DARWIN_FIXTURE_EXCEPTION_SET:
            if (!checked32(input->x[0]) || !checked32(input->x[1]))
                return IOS27_MACH_BAD_ARGUMENT;
            status = ios27_mach_v2_exception_set(space, task,
                                                  (uint32_t)input->x[0],
                                                  (uint32_t)input->x[1]);
            break;
        case IOS27_DARWIN_FIXTURE_EXCEPTION_GET:
            if (!checked32(input->x[0])) return IOS27_MACH_BAD_ARGUMENT;
            status = ios27_mach_v2_exception_get(space, task,
                                                  (uint32_t)input->x[0], &a);
            if (status == IOS27_MACH_OK) reply.x[0] = a;
            break;
        case IOS27_DARWIN_FIXTURE_EXCEPTION_RAISE:
            if (!checked32(input->x[0]) || !checked32(input->x[1]) ||
                !checked32(input->x[2]))
                return IOS27_MACH_BAD_ARGUMENT;
            status = ios27_mach_v2_exception_raise(space, task,
                                            (uint32_t)input->x[0],
                                            (uint32_t)input->x[1],
                                            (uint32_t)input->x[2]);
            break;
        case IOS27_DARWIN_FIXTURE_VM_COPY:
        case IOS27_DARWIN_FIXTURE_VM_ZERO: {
            uint64_t start = input->x[0];
            uint64_t dest = selector == IOS27_DARWIN_FIXTURE_VM_COPY ?
                            input->x[1] : input->x[0];
            uint64_t count = selector == IOS27_DARWIN_FIXTURE_VM_COPY ?
                             input->x[2] : input->x[1];
            if (!arena || count > IOS27_DARWIN_FIXTURE_VM_MAX ||
                dest > (uint64_t)arena_size ||
                count > (uint64_t)arena_size - dest)
                return IOS27_MACH_BAD_ARGUMENT;
            if (selector == IOS27_DARWIN_FIXTURE_VM_COPY) {
                if (start > (uint64_t)arena_size ||
                    count > (uint64_t)arena_size - start)
                    return IOS27_MACH_BAD_ARGUMENT;
                memmove(arena + (size_t)dest, arena + (size_t)start,
                        (size_t)count);
            } else {
                memset(arena + (size_t)dest, 0, (size_t)count);
            }
            reply.x[0] = count;
            status = IOS27_MACH_OK;
            break;
        }
        default:
            return IOS27_MACH_UNSUPPORTED;
    }
    if (status == IOS27_MACH_OK || status == IOS27_MACH_TOO_LARGE)
        *output = reply;
    return status;
}
