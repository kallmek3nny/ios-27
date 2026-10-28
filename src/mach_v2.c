#include "ios27/mach_v2.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define FIRST_NAME 100u
#define TASK_INDEX(task) ((task) - 1u)

typedef struct {
    uint32_t name;
    uint16_t sends;
    uint16_t once;
    uint16_t pending_sends;
    uint16_t pending_once;
    uint8_t object; /* port index + 1, zero is an unused slot */
    uint8_t receive;
} local_right;

typedef struct {
    uint8_t bytes[IOS27_MACH_MSG_MAX];
    uint16_t length;
    uint8_t descriptor_count;
    uint8_t descriptor_objects[IOS27_MACH_V2_DESCRIPTORS];
    uint8_t reply_object;
} queued_wire;

typedef struct {
    uint8_t alive;
    uint8_t owner;
    uint8_t head;
    uint8_t count;
    queued_wire queue[IOS27_MACH_QUEUE_MAX];
} port_object;

struct ios27_mach_v2 {
    uint32_t next_name[IOS27_MACH_TASK_MAX];
    local_right names[IOS27_MACH_TASK_MAX][IOS27_MACH_V2_NAMES];
    port_object ports[IOS27_MACH_PORT_MAX];
};

_Static_assert(sizeof(ios27_mach_header) == 24u, "Mach wire header");
_Static_assert(sizeof(ios27_mach_port_descriptor) == 12u, "Mach descriptor");

static int valid_task(uint32_t task) {
    return task != 0u && task <= IOS27_MACH_TASK_MAX;
}
static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8u));
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static void wr32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0u; i < 4u; ++i) p[i] = (uint8_t)(v >> (8u * i));
}
/* Reject overlapping caller buffers before mutating a queue or right.
 * Invalid/overflowing host ranges fail closed; the guest ABI separately
 * enforces its bounded, non-aliasing offset-based arena. */
static int ranges_overlap(const void *a, size_t n,
                          const void *b, size_t m) {
    uintptr_t x = (uintptr_t)a;
    uintptr_t y = (uintptr_t)b;
    if (n > UINTPTR_MAX - x || m > UINTPTR_MAX - y)
        return 1;
    return n && m && x < y + m && y < x + n;
}
static local_right *by_name(ios27_mach_v2 *s, uint32_t task, uint32_t name) {
    if (!s || !valid_task(task) || !name) return NULL;
    for (size_t i = 0u; i < IOS27_MACH_V2_NAMES; ++i) {
        local_right *r = &s->names[TASK_INDEX(task)][i];
        if (r->object && r->name == name) return r;
    }
    return NULL;
}
static const local_right *by_name_const(const ios27_mach_v2 *s,
                                       uint32_t task, uint32_t name) {
    return by_name((ios27_mach_v2 *)(void *)s, task, name);
}
static local_right *by_object(ios27_mach_v2 *s, uint32_t task, uint8_t obj) {
    if (!s || !valid_task(task) || !obj) return NULL;
    for (size_t i = 0u; i < IOS27_MACH_V2_NAMES; ++i) {
        local_right *r = &s->names[TASK_INDEX(task)][i];
        if (r->object == obj) return r;
    }
    return NULL;
}
static unsigned empty_names(const ios27_mach_v2 *s, uint32_t task) {
    unsigned n = 0u;
    if (!valid_task(task)) return 0u;
    for (size_t i = 0u; i < IOS27_MACH_V2_NAMES; ++i)
        if (!s->names[TASK_INDEX(task)][i].object) ++n;
    return n;
}
static local_right *reserve_name(ios27_mach_v2 *s, uint32_t task,
                                 uint8_t obj) {
    local_right *r = by_object(s, task, obj);
    if (r) return r;
    if (s->next_name[TASK_INDEX(task)] == UINT32_MAX) return NULL;
    for (size_t i = 0u; i < IOS27_MACH_V2_NAMES; ++i) {
        r = &s->names[TASK_INDEX(task)][i];
        if (!r->object) {
            memset(r, 0, sizeof(*r));
            r->object = obj;
            r->name = s->next_name[TASK_INDEX(task)]++;
            return r;
        }
    }
    return NULL;
}
static void prune(local_right *r) {
    if (r && r->object && !r->receive && !r->sends && !r->once &&
        !r->pending_sends && !r->pending_once)
        memset(r, 0, sizeof(*r));
}
static port_object *object_for(ios27_mach_v2 *s, const local_right *r) {
    if (!s || !r || !r->object || r->object > IOS27_MACH_PORT_MAX)
        return NULL;
    port_object *obj = &s->ports[r->object - 1u];
    return obj->alive ? obj : NULL;
}
static local_right *receive_right(ios27_mach_v2 *s, uint32_t owner,
                                  uint32_t name) {
    local_right *r = by_name(s, owner, name);
    port_object *obj = object_for(s, r);
    return (r && r->receive && obj && obj->owner == owner) ? r : NULL;
}
static int may_arrive(ios27_mach_v2 *s, uint32_t task, uint8_t obj,
                      int once) {
    local_right *r = by_object(s, task, obj);
    if (!r)
        return empty_names(s, task) && s->next_name[TASK_INDEX(task)] != UINT32_MAX;
    if (once)
        return (uint32_t)r->once + r->pending_once < IOS27_MACH_RIGHT_MAX;
    return (uint32_t)r->sends + r->pending_sends < IOS27_MACH_RIGHT_MAX;
}

ios27_mach_v2 *ios27_mach_v2_new(void) {
    ios27_mach_v2 *s = calloc(1u, sizeof(*s));
    if (s)
        for (size_t i = 0u; i < IOS27_MACH_TASK_MAX; ++i)
            s->next_name[i] = FIRST_NAME;
    return s;
}
void ios27_mach_v2_free(ios27_mach_v2 *s) {
    free(s);
}
int ios27_mach_v2_allocate(ios27_mach_v2 *s, uint32_t owner,
                           uint32_t *local_name) {
    if (!s || !valid_task(owner) || !local_name)
        return IOS27_MACH_BAD_ARGUMENT;
    for (size_t i = 0u; i < IOS27_MACH_PORT_MAX; ++i)
        if (!s->ports[i].alive) {
            local_right *r = reserve_name(s, owner, (uint8_t)(i + 1u));
            if (!r) return IOS27_MACH_LIMIT;
            if (r->receive) return IOS27_MACH_BAD_RIGHT;
            memset(&s->ports[i], 0, sizeof(s->ports[i]));
            s->ports[i].alive = 1u;
            s->ports[i].owner = (uint8_t)owner;
            r->receive = 1u;
            *local_name = r->name;
            return IOS27_MACH_OK;
        }
    return IOS27_MACH_LIMIT;
}
int ios27_mach_v2_make_send(ios27_mach_v2 *s, uint32_t owner,
                            uint32_t receive_name, uint32_t recipient,
                            uint32_t *recipient_name) {
    local_right *origin, *recipient_right;
    if (!s || !valid_task(owner) || !valid_task(recipient) || !recipient_name)
        return IOS27_MACH_BAD_ARGUMENT;
    origin = receive_right(s, owner, receive_name);
    if (!origin) return IOS27_MACH_BAD_RIGHT;
    if (!may_arrive(s, recipient, origin->object, 0))
        return IOS27_MACH_LIMIT;
    recipient_right = reserve_name(s, recipient, origin->object);
    if (!recipient_right) return IOS27_MACH_LIMIT;
    ++recipient_right->sends;
    *recipient_name = recipient_right->name;
    return IOS27_MACH_OK;
}
int ios27_mach_v2_drop_send(ios27_mach_v2 *s, uint32_t task, uint32_t name) {
    local_right *r = by_name(s, task, name);
    if (!s || !valid_task(task)) return IOS27_MACH_BAD_ARGUMENT;
    if (!r || !r->sends) return IOS27_MACH_BAD_RIGHT;
    --r->sends;
    prune(r);
    return IOS27_MACH_OK;
}
int ios27_mach_v2_drop_send_once(ios27_mach_v2 *s, uint32_t task,
                                 uint32_t name) {
    local_right *r = by_name(s, task, name);
    if (!s || !valid_task(task)) return IOS27_MACH_BAD_ARGUMENT;
    if (!r || !r->once) return IOS27_MACH_BAD_RIGHT;
    --r->once;
    prune(r);
    return IOS27_MACH_OK;
}
int ios27_mach_v2_rights(const ios27_mach_v2 *s, uint32_t task,
                         uint32_t name, uint32_t *send, uint32_t *send_once,
                         unsigned *receive) {
    const local_right *r = by_name_const(s, task, name);
    if (!s || !valid_task(task) || !send || !send_once || !receive)
        return IOS27_MACH_BAD_ARGUMENT;
    if (!r) return IOS27_MACH_BAD_RIGHT;
    *send = r->sends;
    *send_once = r->once;
    *receive = r->receive;
    return IOS27_MACH_OK;
}
/* Compute the exact pending credits that discarding these messages will
 * cancel. Credits may be canceled only when their destination queue itself
 * is being retired; grants waiting in a different live queue are immutable. */
static void discarded_credits(const port_object *port,
                              uint16_t sends[IOS27_MACH_PORT_MAX],
                              uint16_t once[IOS27_MACH_PORT_MAX]) {
    for (size_t i = 0u; i < port->count; ++i) {
        const queued_wire *m =
            &port->queue[(port->head + i) % IOS27_MACH_QUEUE_MAX];
        for (size_t j = 0u; j < m->descriptor_count; ++j)
            ++sends[m->descriptor_objects[j] - 1u];
        if (m->reply_object) ++once[m->reply_object - 1u];
    }
}
static void cancel_pending(ios27_mach_v2 *s, uint32_t task, uint8_t obj,
                           int once) {
    local_right *r = by_object(s, task, obj);
    if (!r) return;
    if (once) {
        if (r->pending_once) --r->pending_once;
    } else {
        if (r->pending_sends) --r->pending_sends;
    }
    prune(r);
}
int ios27_mach_v2_destroy(ios27_mach_v2 *s, uint32_t owner,
                          uint32_t receive_name) {
    local_right *right;
    port_object *obj;
    uint8_t index;
    if (!s || !valid_task(owner)) return IOS27_MACH_BAD_ARGUMENT;
    right = receive_right(s, owner, receive_name);
    if (!right) return IOS27_MACH_BAD_RIGHT;
    index = right->object;
    obj = object_for(s, right);
    if (!obj) return IOS27_MACH_BAD_RIGHT;
    /* Pending grants from this port's own queue are discarded with it.
     * Grants from any other live queue must still block destruction. */
    uint16_t canceled_sends[IOS27_MACH_PORT_MAX] = {0};
    uint16_t canceled_once[IOS27_MACH_PORT_MAX] = {0};
    discarded_credits(obj, canceled_sends, canceled_once);
    for (uint32_t task = 1u; task <= IOS27_MACH_TASK_MAX; ++task) {
        const local_right *r = by_object(s, task, index);
        if (r && (r->pending_sends !=
                  (task == owner ? canceled_sends[index - 1u] : 0u) ||
                  r->pending_once !=
                  (task == owner ? canceled_once[index - 1u] : 0u)))
            return IOS27_MACH_BAD_RIGHT;
    }
    for (size_t i = 0u; i < obj->count; ++i) {
        queued_wire *m = &obj->queue[(obj->head + i) % IOS27_MACH_QUEUE_MAX];
        for (size_t j = 0u; j < m->descriptor_count; ++j)
            cancel_pending(s, owner, m->descriptor_objects[j], 0);
        if (m->reply_object)
            cancel_pending(s, owner, m->reply_object, 1);
    }
    memset(obj, 0, sizeof(*obj));
    for (uint32_t task = 1u; task <= IOS27_MACH_TASK_MAX; ++task) {
        local_right *r = by_object(s, task, index);
        if (r) memset(r, 0, sizeof(*r));
    }
    return IOS27_MACH_OK;
}
/*
 * Transactional synthetic task cleanup. Cancel only grants in queues
 * owned by the retiring task; any grant waiting in a foreign live queue
 * must keep teardown from silently invalidating its destination credit.
 * This does not model XNU dead-name/notification behavior.
 */
int ios27_mach_v2_task_teardown(ios27_mach_v2 *s, uint32_t task) {
    uint8_t owned[IOS27_MACH_PORT_MAX] = {0};
    uint16_t canceled_sends[IOS27_MACH_PORT_MAX] = {0};
    uint16_t canceled_once[IOS27_MACH_PORT_MAX] = {0};
    if (!s || !valid_task(task)) return IOS27_MACH_BAD_ARGUMENT;

    for (size_t i = 0u; i < IOS27_MACH_PORT_MAX; ++i) {
        const port_object *obj = &s->ports[i];
        if (obj->alive && obj->owner == task) {
            owned[i] = 1u;
            discarded_credits(obj, canceled_sends, canceled_once);
        }
    }
    /* Only credits for ports this task owns may be canceled by tearing
     * down its queues. Transfers of foreign-owned rights stay guarded by
     * the phase-11 ABI contract, even if their message is queued locally. */
    for (size_t i = 0u; i < IOS27_MACH_V2_NAMES; ++i) {
        const local_right *r = &s->names[TASK_INDEX(task)][i];
        if (!r->object) continue;
        if (!owned[r->object - 1u]) {
            if (r->pending_sends || r->pending_once)
                return IOS27_MACH_BAD_RIGHT;
        } else if (r->pending_sends != canceled_sends[r->object - 1u] ||
                   r->pending_once != canceled_once[r->object - 1u]) {
            return IOS27_MACH_BAD_RIGHT;
        }
    }
    for (size_t i = 0u; i < IOS27_MACH_PORT_MAX; ++i) {
        if (!owned[i]) continue;
        for (uint32_t other = 1u; other <= IOS27_MACH_TASK_MAX; ++other) {
            const local_right *r = by_object(s, other, (uint8_t)(i + 1u));
            if (other != task && r &&
                (r->pending_sends || r->pending_once))
                return IOS27_MACH_BAD_RIGHT;
        }
    }
    /* From here on there is no possible failure: cancel queued grants
     * to foreign ports, then revoke all rights to destroyed objects. */
    for (size_t i = 0u; i < IOS27_MACH_PORT_MAX; ++i) {
        port_object *obj = &s->ports[i];
        if (!owned[i]) continue;
        for (size_t j = 0u; j < obj->count; ++j) {
            const queued_wire *m =
                &obj->queue[(obj->head + j) % IOS27_MACH_QUEUE_MAX];
            for (size_t k = 0u; k < m->descriptor_count; ++k)
                cancel_pending(s, task, m->descriptor_objects[k], 0);
            if (m->reply_object)
                cancel_pending(s, task, m->reply_object, 1);
        }
        memset(obj, 0, sizeof(*obj));
    }
    for (uint32_t owner = 1u; owner <= IOS27_MACH_TASK_MAX; ++owner) {
        for (size_t i = 0u; i < IOS27_MACH_V2_NAMES; ++i) {
            local_right *r = &s->names[TASK_INDEX(owner)][i];
            if ((owner == task) ||
                (r->object && owned[r->object - 1u]))
                memset(r, 0, sizeof(*r));
        }
    }
    return IOS27_MACH_OK;
}


int ios27_mach_v2_send(ios27_mach_v2 *s, uint32_t task,
                       const void *wire, size_t size, uint32_t timeout_ms) {
    const uint8_t *p = wire;
    uint32_t bits, dest_name, reply_name, remote_disposition, local_disposition;
    local_right *dest_right, *reply_right = NULL;
    local_right *descriptors[IOS27_MACH_V2_DESCRIPTORS] = {0};
    uint8_t desc_objects[IOS27_MACH_V2_DESCRIPTORS] = {0};
    uint8_t desc_moves[IOS27_MACH_V2_DESCRIPTORS] = {0};
    port_object *dest;
    uint8_t reply_obj = 0u, reply_move = 0u;
    uint32_t receiver, count = 0u;
    size_t tail;
    unsigned additional = 0u;
    (void)timeout_ms;
    if (!s || !valid_task(task) || !p || size < 24u ||
        size > IOS27_MACH_MSG_MAX || (size & 3u) ||
        ranges_overlap(p, size, s, sizeof(*s)))
        return IOS27_MACH_BAD_ARGUMENT;
    bits = rd32(p);
    remote_disposition = bits & 0xffu;
    local_disposition = (bits >> 8u) & 0xffu;
    dest_name = rd32(p + 8u);
    reply_name = rd32(p + 12u);
    if (rd32(p + 4u) != size || rd32(p + 16u) != 0u ||
        bits & ~(IOS27_MACH_COMPLEX | UINT32_C(0xffff)) ||
        (remote_disposition != IOS27_MACH_COPY_SEND &&
         remote_disposition != IOS27_MACH_MOVE_SEND &&
         remote_disposition != IOS27_MACH_MOVE_SEND_ONCE) ||
        (local_disposition != 0u &&
         local_disposition != IOS27_MACH_MAKE_SEND_ONCE &&
         local_disposition != IOS27_MACH_MOVE_SEND_ONCE) ||
        ((local_disposition == 0u) != (reply_name == 0u)))
        return IOS27_MACH_UNSUPPORTED;
    dest_right = by_name(s, task, dest_name);
    dest = object_for(s, dest_right);
    if (!dest || ((remote_disposition == IOS27_MACH_COPY_SEND ||
                   remote_disposition == IOS27_MACH_MOVE_SEND) &&
                  !dest_right->sends) ||
        (remote_disposition == IOS27_MACH_MOVE_SEND_ONCE &&
         !dest_right->once))
        return IOS27_MACH_BAD_RIGHT;
    receiver = dest->owner;
    if (local_disposition) {
        reply_move = (uint8_t)(local_disposition == IOS27_MACH_MOVE_SEND_ONCE);
        reply_right = reply_move ? by_name(s, task, reply_name) :
                                   receive_right(s, task, reply_name);
        if (!reply_right || !object_for(s, reply_right) ||
            (reply_move && !reply_right->once))
            return IOS27_MACH_BAD_RIGHT;
        reply_obj = reply_right->object;
        if (!may_arrive(s, receiver, reply_obj, 1))
            return IOS27_MACH_LIMIT;
    }
    if (bits & IOS27_MACH_COMPLEX) {
        if (size < 28u) return IOS27_MACH_BAD_ARGUMENT;
        count = rd32(p + 24u);
        if (!count || count > IOS27_MACH_V2_DESCRIPTORS ||
            size < 28u + 12u * count)
            return IOS27_MACH_UNSUPPORTED;
        for (uint32_t i = 0u; i < count; ++i) {
            size_t offset = 28u + 12u * i;
            uint8_t disposition = p[offset + 10u];
            if (rd32(p + offset + 4u) || rd16(p + offset + 8u) ||
                p[offset + 11u] != IOS27_MACH_PORT_DESCRIPTOR ||
                (disposition != IOS27_MACH_COPY_SEND &&
                 disposition != IOS27_MACH_MOVE_SEND))
                return IOS27_MACH_UNSUPPORTED;
            descriptors[i] = by_name(s, task, rd32(p + offset));
            if (!object_for(s, descriptors[i]) || !descriptors[i]->sends)
                return IOS27_MACH_BAD_RIGHT;
            desc_objects[i] = descriptors[i]->object;
            desc_moves[i] = (uint8_t)(disposition == IOS27_MACH_MOVE_SEND);
        }
    }

    /* Every source debit is counted before mutating any rights. In
     * particular, the destination and several descriptors may name the
     * same send right. A single uref cannot satisfy repeated MOVE_SEND. */
    for (uint32_t i = 0u; i < count; ++i) {
        uint32_t needed = 0u;
        if (!desc_moves[i]) continue;
        for (uint32_t j = 0u; j < count; ++j)
            if (desc_moves[j] && descriptors[j] == descriptors[i])
                ++needed;
        if (remote_disposition == IOS27_MACH_MOVE_SEND &&
            dest_right == descriptors[i])
            ++needed;
        if (descriptors[i]->sends < needed)
            return IOS27_MACH_BAD_RIGHT;
    }
    if (reply_move && remote_disposition == IOS27_MACH_MOVE_SEND_ONCE &&
        reply_right == dest_right && dest_right->once < 2u)
        return IOS27_MACH_BAD_RIGHT;
    if (dest->count == IOS27_MACH_QUEUE_MAX)
        return IOS27_MACH_TIMEOUT;

    /* Aggregate arrival credits for repeated references to one object.
     * Checking each independently would overbook the right limit. */
    for (uint32_t i = 0u; i < count; ++i) {
        uint32_t arriving = 0u;
        local_right *r = by_object(s, receiver, desc_objects[i]);
        for (uint32_t j = 0u; j < count; ++j)
            if (desc_objects[j] == desc_objects[i]) ++arriving;
        if (r && (uint32_t)r->sends + r->pending_sends + arriving >
                 IOS27_MACH_RIGHT_MAX)
            return IOS27_MACH_LIMIT;
    }
    /* Reserve one local name per *distinct* object, whether the new
     * credits are send, send-once, or both. */
    if (reply_obj && !by_object(s, receiver, reply_obj)) ++additional;
    for (uint32_t i = 0u; i < count; ++i) {
        int previous = desc_objects[i] == reply_obj;
        for (uint32_t j = 0u; j < i; ++j)
            if (desc_objects[j] == desc_objects[i]) previous = 1;
        if (!previous && !by_object(s, receiver, desc_objects[i]))
            ++additional;
    }
    if (empty_names(s, receiver) < additional ||
        (additional && (uint64_t)s->next_name[TASK_INDEX(receiver)] +
                       additional > UINT32_MAX))
        return IOS27_MACH_LIMIT;

    /* The transaction cannot fail after reservation preflight. */
    if (reply_obj)
        ++reserve_name(s, receiver, reply_obj)->pending_once;
    for (uint32_t i = 0u; i < count; ++i)
        ++reserve_name(s, receiver, desc_objects[i])->pending_sends;
    if (reply_move) --reply_right->once;
    for (uint32_t i = 0u; i < count; ++i)
        if (desc_moves[i]) --descriptors[i]->sends;
    if (remote_disposition == IOS27_MACH_MOVE_SEND)
        --dest_right->sends;
    else if (remote_disposition == IOS27_MACH_MOVE_SEND_ONCE)
        --dest_right->once;
    if (reply_move) prune(reply_right);
    for (uint32_t i = 0u; i < count; ++i)
        if (desc_moves[i]) prune(descriptors[i]);
    prune(dest_right);

    tail = (dest->head + dest->count) % IOS27_MACH_QUEUE_MAX;
    queued_wire *slot = &dest->queue[tail];
    memset(slot, 0, sizeof(*slot));
    memcpy(slot->bytes, p, size);
    slot->length = (uint16_t)size;
    slot->reply_object = reply_obj;
    slot->descriptor_count = (uint8_t)count;
    memcpy(slot->descriptor_objects, desc_objects, count);
    ++dest->count;
    return IOS27_MACH_OK;
}
int ios27_mach_v2_receive(ios27_mach_v2 *s, uint32_t task,
                          uint32_t receive_name, void *out,
                          size_t *inout_size, uint32_t timeout_ms) {
    local_right *right;
    port_object *obj;
    queued_wire *msg;
    local_right *reply;
    local_right *descriptors[IOS27_MACH_V2_DESCRIPTORS] = {0};
    uint8_t *output = out;
    (void)timeout_ms;
    if (!s || !valid_task(task) || !out || !inout_size ||
        ranges_overlap(inout_size, sizeof(*inout_size), s, sizeof(*s)))
        return IOS27_MACH_BAD_ARGUMENT;
    right = receive_right(s, task, receive_name);
    obj = object_for(s, right);
    if (!obj) return IOS27_MACH_BAD_RIGHT;
    if (!obj->count) return IOS27_MACH_TIMEOUT;
    msg = &obj->queue[obj->head];
    if (ranges_overlap(output, msg->length, s, sizeof(*s)) ||
        ranges_overlap(output, msg->length,
                       inout_size, sizeof(*inout_size)))
        return IOS27_MACH_BAD_ARGUMENT;
    if (*inout_size < msg->length) {
        *inout_size = msg->length;
        return IOS27_MACH_TOO_LARGE;
    }
    reply = msg->reply_object ? by_object(s, task, msg->reply_object) : NULL;
    if (msg->reply_object && (!reply || !reply->pending_once))
        return IOS27_MACH_BAD_RIGHT;
    for (size_t i = 0u; i < msg->descriptor_count; ++i) {
        uint32_t needed = 0u;
        descriptors[i] = by_object(s, task, msg->descriptor_objects[i]);
        for (size_t j = 0u; j < msg->descriptor_count; ++j)
            if (msg->descriptor_objects[i] == msg->descriptor_objects[j])
                ++needed;
        if (!descriptors[i] || descriptors[i]->pending_sends < needed)
            return IOS27_MACH_BAD_RIGHT;
    }
    memcpy(output, msg->bytes, msg->length);
    wr32(output, (rd32(output) & IOS27_MACH_COMPLEX) |
                 (reply ? IOS27_MACH_MOVE_SEND_ONCE : 0u));
    wr32(output + 8u, reply ? reply->name : 0u);
    wr32(output + 12u, receive_name);
    for (size_t i = 0u; i < msg->descriptor_count; ++i) {
        size_t offset = 28u + 12u * i;
        wr32(output + offset, descriptors[i]->name);
        output[offset + 10u] = IOS27_MACH_MOVE_SEND;
        --descriptors[i]->pending_sends;
        ++descriptors[i]->sends;
    }
    if (reply) {
        --reply->pending_once;
        ++reply->once;
    }
    *inout_size = msg->length;
    memset(msg, 0, sizeof(*msg));
    obj->head = (uint8_t)((obj->head + 1u) % IOS27_MACH_QUEUE_MAX);
    --obj->count;
    return IOS27_MACH_OK;
}
