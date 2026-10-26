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
    uint8_t descriptor_object;
    uint8_t descriptor_move;
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
    /* Do not invalidate a port that another queued message will grant. */
    for (uint32_t task = 1u; task <= IOS27_MACH_TASK_MAX; ++task) {
        local_right *r = by_object(s, task, index);
        if (r && (r->pending_sends || r->pending_once))
            return IOS27_MACH_BAD_RIGHT;
    }
    for (size_t i = 0u; i < obj->count; ++i) {
        queued_wire *m = &obj->queue[(obj->head + i) % IOS27_MACH_QUEUE_MAX];
        if (m->descriptor_object)
            cancel_pending(s, owner, m->descriptor_object, 0);
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
int ios27_mach_v2_send(ios27_mach_v2 *s, uint32_t task,
                       const void *wire, size_t size, uint32_t timeout_ms) {
    const uint8_t *p = wire;
    uint32_t bits, dest_name, reply_name, remote_disposition, local_disposition;
    local_right *dest_right, *reply_right = NULL, *descriptor_right = NULL;
    local_right *pending_reply, *pending_descriptor;
    port_object *dest;
    uint8_t reply_obj = 0u, desc_obj = 0u, desc_move = 0u;
    uint32_t receiver;
    size_t tail;
    unsigned additional = 0u;
    (void)timeout_ms;
    if (!s || !valid_task(task) || !p || size < 24u ||
        size > IOS27_MACH_MSG_MAX || (size & 3u))
        return IOS27_MACH_BAD_ARGUMENT;
    bits = rd32(p);
    remote_disposition = bits & 0xffu;
    local_disposition = (bits >> 8u) & 0xffu;
    dest_name = rd32(p + 8u);
    reply_name = rd32(p + 12u);
    if (rd32(p + 4u) != size || rd32(p + 16u) != 0u ||
        bits & ~(IOS27_MACH_COMPLEX | UINT32_C(0xffff)) ||
        (remote_disposition != IOS27_MACH_COPY_SEND &&
         remote_disposition != IOS27_MACH_MOVE_SEND_ONCE) ||
        (local_disposition != 0u &&
         local_disposition != IOS27_MACH_MAKE_SEND_ONCE) ||
        ((local_disposition == 0u) != (reply_name == 0u)))
        return IOS27_MACH_UNSUPPORTED;
    dest_right = by_name(s, task, dest_name);
    dest = object_for(s, dest_right);
    if (!dest || (remote_disposition == IOS27_MACH_COPY_SEND &&
                  !dest_right->sends) ||
        (remote_disposition == IOS27_MACH_MOVE_SEND_ONCE &&
         !dest_right->once))
        return IOS27_MACH_BAD_RIGHT;
    receiver = dest->owner;
    if (local_disposition) {
        reply_right = receive_right(s, task, reply_name);
        if (!reply_right) return IOS27_MACH_BAD_RIGHT;
        reply_obj = reply_right->object;
        if (!may_arrive(s, receiver, reply_obj, 1))
            return IOS27_MACH_LIMIT;
    }
    if (bits & IOS27_MACH_COMPLEX) {
        uint32_t name;
        if (size < 40u) return IOS27_MACH_BAD_ARGUMENT;
        if (rd32(p + 24u) != 1u || rd32(p + 32u) ||
            rd16(p + 36u) || p[39u] != IOS27_MACH_PORT_DESCRIPTOR ||
            (p[38u] != IOS27_MACH_COPY_SEND &&
             p[38u] != IOS27_MACH_MOVE_SEND))
            return IOS27_MACH_UNSUPPORTED;
        name = rd32(p + 28u);
        descriptor_right = by_name(s, task, name);
        if (!object_for(s, descriptor_right) || !descriptor_right->sends)
            return IOS27_MACH_BAD_RIGHT;
        desc_obj = descriptor_right->object;
        desc_move = (uint8_t)(p[38u] == IOS27_MACH_MOVE_SEND);
        if (!may_arrive(s, receiver, desc_obj, 0))
            return IOS27_MACH_LIMIT;
    }
    if (dest->count == IOS27_MACH_QUEUE_MAX)
        return IOS27_MACH_TIMEOUT;
    /* Preflight capacity for up to two distinct *new* local aliases. */
    if (reply_obj && !by_object(s, receiver, reply_obj)) ++additional;
    if (desc_obj && desc_obj != reply_obj &&
        !by_object(s, receiver, desc_obj)) ++additional;
    if (empty_names(s, receiver) < additional ||
        (additional && s->next_name[TASK_INDEX(receiver)] >=
         UINT32_MAX - (additional - 1u)))
        return IOS27_MACH_LIMIT;

    /* No more error paths after this point; all allocations are preflighted. */
    pending_reply = reply_obj ? reserve_name(s, receiver, reply_obj) : NULL;
    pending_descriptor = desc_obj ? reserve_name(s, receiver, desc_obj) : NULL;
    if (pending_reply) ++pending_reply->pending_once;
    if (pending_descriptor) ++pending_descriptor->pending_sends;
    if (desc_move) {
        --descriptor_right->sends;
        prune(descriptor_right);
    }
    if (remote_disposition == IOS27_MACH_MOVE_SEND_ONCE) {
        --dest_right->once;
        prune(dest_right);
    }
    tail = (dest->head + dest->count) % IOS27_MACH_QUEUE_MAX;
    queued_wire *slot = &dest->queue[tail];
    memset(slot, 0, sizeof(*slot));
    memcpy(slot->bytes, p, size);
    slot->length = (uint16_t)size;
    slot->reply_object = reply_obj;
    slot->descriptor_object = desc_obj;
    slot->descriptor_move = desc_move;
    ++dest->count;
    return IOS27_MACH_OK;
}
int ios27_mach_v2_receive(ios27_mach_v2 *s, uint32_t task,
                          uint32_t receive_name, void *out,
                          size_t *inout_size, uint32_t timeout_ms) {
    local_right *right;
    port_object *obj;
    queued_wire *msg;
    local_right *reply, *descriptor;
    uint8_t *output = out;
    (void)timeout_ms;
    if (!s || !valid_task(task) || !out || !inout_size)
        return IOS27_MACH_BAD_ARGUMENT;
    right = receive_right(s, task, receive_name);
    obj = object_for(s, right);
    if (!obj) return IOS27_MACH_BAD_RIGHT;
    if (!obj->count) return IOS27_MACH_TIMEOUT;
    msg = &obj->queue[obj->head];
    if (*inout_size < msg->length) {
        *inout_size = msg->length;
        return IOS27_MACH_TOO_LARGE;
    }
    reply = msg->reply_object ? by_object(s, task, msg->reply_object) : NULL;
    descriptor = msg->descriptor_object ?
        by_object(s, task, msg->descriptor_object) : NULL;
    if ((msg->reply_object && (!reply || !reply->pending_once)) ||
        (msg->descriptor_object && (!descriptor || !descriptor->pending_sends)))
        return IOS27_MACH_BAD_RIGHT;
    memcpy(output, msg->bytes, msg->length);
    wr32(output, rd32(output) & IOS27_MACH_COMPLEX);
    wr32(output + 8u, reply ? reply->name : 0u);
    wr32(output + 12u, receive_name);
    if (descriptor) {
        wr32(output + 28u, descriptor->name);
        --descriptor->pending_sends;
        ++descriptor->sends;
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
