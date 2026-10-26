#include "ios27/mach_ipc.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define IOS27_MACH_FIRST_NAME 100u

/* Build-time ABI guarantees; wire fields are copied, never cast to user bytes. */
_Static_assert(sizeof(ios27_mach_header) == 24u, "Mach header ABI size");
_Static_assert(offsetof(ios27_mach_header, id) == 20u, "Mach header layout");
_Static_assert(sizeof(ios27_mach_port_descriptor) == 12u,
               "Mach port descriptor ABI size");
_Static_assert(offsetof(ios27_mach_port_descriptor, disposition) == 10u,
               "Mach descriptor layout");

typedef struct {
    uint8_t bytes[IOS27_MACH_MSG_MAX];
    uint16_t length;
    /* A staged in-transit right, not yet owned by the receiving task. */
    uint32_t transferred_port;
    uint8_t transfer_disposition;
} queued_message;

typedef struct {
    uint32_t name;
    uint32_t receive_owner;
    uint16_t send_refs[IOS27_MACH_TASK_MAX];
    uint16_t in_flight;
    uint8_t count;
    uint8_t head;
    queued_message messages[IOS27_MACH_QUEUE_MAX];
} mach_port;

struct ios27_mach_space {
    uint32_t next_name;
    mach_port ports[IOS27_MACH_PORT_MAX];
};

static int valid_task(uint32_t task) {
    return task >= 1u && task <= IOS27_MACH_TASK_MAX;
}
static uint16_t read16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}
static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static void write32(uint8_t *p, uint32_t n) {
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8u);
    p[2] = (uint8_t)(n >> 16u);
    p[3] = (uint8_t)(n >> 24u);
}
static mach_port *lookup(ios27_mach_space *space, uint32_t name) {
    if (!space || !name) return NULL;
    for (size_t i = 0u; i < IOS27_MACH_PORT_MAX; ++i)
        if (space->ports[i].name == name)
            return &space->ports[i];
    return NULL;
}
static const mach_port *lookup_const(const ios27_mach_space *space,
                                     uint32_t name) {
    if (!space || !name) return NULL;
    for (size_t i = 0u; i < IOS27_MACH_PORT_MAX; ++i)
        if (space->ports[i].name == name)
            return &space->ports[i];
    return NULL;
}
ios27_mach_space *ios27_mach_space_new(void) {
    ios27_mach_space *space = calloc(1u, sizeof(*space));
    if (space) space->next_name = IOS27_MACH_FIRST_NAME;
    return space;
}
void ios27_mach_space_free(ios27_mach_space *space) {
    free(space);
}
int ios27_mach_port_allocate(ios27_mach_space *space, uint32_t owner,
                            uint32_t *port_name) {
    if (!space || !valid_task(owner) || !port_name)
        return IOS27_MACH_BAD_ARGUMENT;
    if (space->next_name == UINT32_MAX)
        return IOS27_MACH_LIMIT;
    for (size_t i = 0u; i < IOS27_MACH_PORT_MAX; ++i) {
        mach_port *port = &space->ports[i];
        if (!port->name) {
            memset(port, 0, sizeof(*port));
            port->name = space->next_name++;
            port->receive_owner = owner;
            *port_name = port->name;
            return IOS27_MACH_OK;
        }
    }
    return IOS27_MACH_LIMIT;
}
int ios27_mach_port_make_send(ios27_mach_space *space, uint32_t owner,
                             uint32_t name, uint32_t recipient) {
    mach_port *port;
    if (!space || !valid_task(owner) || !valid_task(recipient))
        return IOS27_MACH_BAD_ARGUMENT;
    port = lookup(space, name);
    if (!port || port->receive_owner != owner)
        return IOS27_MACH_BAD_RIGHT;
    /* In-flight messages reserve future receiver send-right capacity.
     * Count all reservations conservatively: no delivery can overfill. */
    if ((uint32_t)port->send_refs[recipient - 1u] +
        port->in_flight >= IOS27_MACH_RIGHT_MAX)
        return IOS27_MACH_LIMIT;
    ++port->send_refs[recipient - 1u];
    return IOS27_MACH_OK;
}
int ios27_mach_port_drop_send(ios27_mach_space *space, uint32_t task,
                             uint32_t name) {
    mach_port *port;
    if (!space || !valid_task(task)) return IOS27_MACH_BAD_ARGUMENT;
    port = lookup(space, name);
    if (!port || !port->send_refs[task - 1u])
        return IOS27_MACH_BAD_RIGHT;
    --port->send_refs[task - 1u];
    return IOS27_MACH_OK;
}
int ios27_mach_send_refs(const ios27_mach_space *space, uint32_t task,
                         uint32_t name, uint32_t *count) {
    const mach_port *port;
    if (!space || !valid_task(task) || !count)
        return IOS27_MACH_BAD_ARGUMENT;
    port = lookup_const(space, name);
    if (!port) return IOS27_MACH_BAD_RIGHT;
    *count = port->send_refs[task - 1u];
    return IOS27_MACH_OK;
}
int ios27_mach_port_destroy(ios27_mach_space *space, uint32_t owner,
                            uint32_t name) {
    mach_port *port;
    if (!space || !valid_task(owner)) return IOS27_MACH_BAD_ARGUMENT;
    port = lookup(space, name);
    if (!port || port->receive_owner != owner)
        return IOS27_MACH_BAD_RIGHT;
    /* Prevent deleting a port while another queue owns a pending right. */
    if (port->in_flight) return IOS27_MACH_BAD_RIGHT;
    for (size_t i = 0u; i < port->count; ++i) {
        const queued_message *msg =
            &port->messages[(port->head + i) % IOS27_MACH_QUEUE_MAX];
        if (msg->transferred_port) {
            mach_port *other = lookup(space, msg->transferred_port);
            if (other && other->in_flight) --other->in_flight;
        }
    }
    memset(port, 0, sizeof(*port));
    return IOS27_MACH_OK;
}
int ios27_mach_send(ios27_mach_space *space, uint32_t sender,
                    const void *message, size_t length,
                    uint32_t timeout_ms) {
    const uint8_t *bytes = message;
    uint32_t bits, dest_name;
    mach_port *destination;
    mach_port *target = NULL;
    uint32_t transfer_name = 0u;
    uint8_t disposition = 0u;
    queued_message *slot;
    size_t tail;
    (void)timeout_ms; /* Poll-only deterministic fixture implementation. */
    if (!space || !valid_task(sender) || !message ||
        length < sizeof(ios27_mach_header) ||
        length > IOS27_MACH_MSG_MAX || (length & 3u))
        return IOS27_MACH_BAD_ARGUMENT;
    bits = read32(bytes);
    dest_name = read32(bytes + 8u);
    if (read32(bytes + 4u) != length ||
        (bits != IOS27_MACH_COPY_SEND &&
         bits != (IOS27_MACH_COMPLEX | IOS27_MACH_COPY_SEND)) ||
        read32(bytes + 12u) != 0u || read32(bytes + 16u) != 0u)
        return IOS27_MACH_UNSUPPORTED;
    destination = lookup(space, dest_name);
    if (!destination || !destination->send_refs[sender - 1u])
        return IOS27_MACH_BAD_RIGHT;
    if (bits & IOS27_MACH_COMPLEX) {
        uint32_t desc_count, desc_name;
        const uint8_t *d;
        if (length < sizeof(ios27_mach_header) + 4u +
                     sizeof(ios27_mach_port_descriptor))
            return IOS27_MACH_BAD_ARGUMENT;
        desc_count = read32(bytes + 24u);
        if (desc_count != 1u) return IOS27_MACH_UNSUPPORTED;
        d = bytes + 28u;
        desc_name = read32(d);
        disposition = d[10u];
        if (read32(d + 4u) != 0u || read16(d + 8u) != 0u ||
            d[11u] != IOS27_MACH_PORT_DESCRIPTOR ||
            (disposition != IOS27_MACH_COPY_SEND &&
             disposition != IOS27_MACH_MOVE_SEND))
            return IOS27_MACH_UNSUPPORTED;
        target = lookup(space, desc_name);
        if (!target || !target->send_refs[sender - 1u])
            return IOS27_MACH_BAD_RIGHT;
        /*
         * Reserve receiver capacity before commit so receipt never fails.
         * An in-flight count also bounds unclaimed rights and keeps the
         * source port alive while queued.
         */
        if ((uint32_t)target->send_refs[destination->receive_owner - 1u] +
            target->in_flight >= IOS27_MACH_RIGHT_MAX)
            return IOS27_MACH_LIMIT;
        transfer_name = desc_name;
    }
    if (destination->count == IOS27_MACH_QUEUE_MAX)
        return IOS27_MACH_TIMEOUT;

    /* Only after ALL checks: enqueue and transfer an in-flight reference. */
    tail = (destination->head + destination->count) % IOS27_MACH_QUEUE_MAX;
    slot = &destination->messages[tail];
    memcpy(slot->bytes, bytes, length);
    slot->length = (uint16_t)length;
    slot->transferred_port = transfer_name;
    slot->transfer_disposition = disposition;
    if (target) {
        ++target->in_flight;
        if (disposition == IOS27_MACH_MOVE_SEND)
            --target->send_refs[sender - 1u];
    }
    ++destination->count;
    return IOS27_MACH_OK;
}
int ios27_mach_receive(ios27_mach_space *space, uint32_t receiver,
                       uint32_t name, void *buffer,
                       size_t *inout_length, uint32_t timeout_ms) {
    mach_port *destination, *target = NULL;
    queued_message *msg;
    size_t length;
    (void)timeout_ms; /* Always immediate: no guest-controlled blocking. */
    if (!space || !valid_task(receiver) || !buffer || !inout_length)
        return IOS27_MACH_BAD_ARGUMENT;
    destination = lookup(space, name);
    if (!destination || destination->receive_owner != receiver)
        return IOS27_MACH_BAD_RIGHT;
    if (!destination->count) return IOS27_MACH_TIMEOUT;
    msg = &destination->messages[destination->head];
    length = msg->length;
    if (*inout_length < length) {
        *inout_length = length;
        return IOS27_MACH_TOO_LARGE; /* No dequeue; no rights changed. */
    }
    if (msg->transferred_port) {
        target = lookup(space, msg->transferred_port);
        if (!target || !target->in_flight ||
            target->send_refs[receiver - 1u] >= IOS27_MACH_RIGHT_MAX)
            return IOS27_MACH_BAD_RIGHT;
    }
    memcpy(buffer, msg->bytes, length);
    /*
     * Received header differs from outgoing: local name is the receiver's
     * port, remote/voucher are null. This is a synthetic contract and
     * intentionally omits reply/send-once rights and kernel trailers.
     */
    write32((uint8_t *)buffer, read32((uint8_t *)buffer) & IOS27_MACH_COMPLEX);
    write32((uint8_t *)buffer + 8u, 0u);
    write32((uint8_t *)buffer + 12u, name);
    if (target) {
        --target->in_flight;
        ++target->send_refs[receiver - 1u];
    }
    memset(msg, 0, sizeof(*msg));
    destination->head = (uint8_t)((destination->head + 1u) %
                                  IOS27_MACH_QUEUE_MAX);
    --destination->count;
    *inout_length = length;
    return IOS27_MACH_OK;
}
