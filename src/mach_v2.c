#include "ios27/mach_v2.h"

#include <limits.h>
#include <stddef.h>
#include <sched.h>
#include <stdatomic.h>
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
    uint8_t watch_dead;
} local_right;

typedef struct {
    uint8_t bytes[IOS27_MACH_MSG_MAX];
    uint16_t length;
    uint8_t descriptor_count;
    uint8_t descriptor_objects[IOS27_MACH_V2_DESCRIPTORS];
    uint16_t descriptor_offsets[IOS27_MACH_V2_DESCRIPTORS];
    uint16_t ool_offset;
    uint16_t ool_size;
    uint8_t ool_present;
    uint8_t ool_bytes[IOS27_MACH_OOL_MAX];
    uint8_t reply_object;
} queued_wire;

typedef struct {
    uint32_t id;
    uint8_t active;
} thread_record;

typedef struct {
    uint8_t alive;
    uint8_t owner;
    uint8_t head;
    uint8_t count;
    uint8_t set_id; /* owner-local set slot + 1, zero = detached */
    uint32_t seqno; /* successful receives, including direct receive */
    queued_wire queue[IOS27_MACH_QUEUE_MAX];
} port_object;

typedef struct {
    uint32_t name;
    uint8_t active;
    uint8_t cursor; /* next port index for round-robin receive */
} port_set;

struct ios27_mach_v2 {
    atomic_flag guard;
    uint32_t next_thread_id;
    thread_record threads[IOS27_MACH_TASK_MAX][IOS27_MACH_V2_THREADS];
    uint32_t exception_names[IOS27_MACH_TASK_MAX][IOS27_MACH_V2_EXCEPTION_SLOTS];
    port_set sets[IOS27_MACH_TASK_MAX][IOS27_MACH_V2_PORT_SETS];
    uint32_t next_name[IOS27_MACH_TASK_MAX];
    local_right names[IOS27_MACH_TASK_MAX][IOS27_MACH_V2_NAMES];
    port_object ports[IOS27_MACH_PORT_MAX];
    uint32_t dead_events[IOS27_MACH_TASK_MAX][IOS27_MACH_V2_NAMES];
    uint8_t dead_head[IOS27_MACH_TASK_MAX];
    uint8_t dead_count[IOS27_MACH_TASK_MAX];
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
static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4u) << 32u);
}
static void wr64(uint8_t *p, uint64_t v) {
    wr32(p, (uint32_t)v);
    wr32(p + 4u, (uint32_t)(v >> 32u));
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
int ios27_mach_v2_validate_external(const ios27_mach_v2 *s,
                                     const void *ptr, size_t length) {
    if (!s || (!ptr && length) ||
        ranges_overlap(ptr, length, s, sizeof(*s)))
        return IOS27_MACH_BAD_ARGUMENT;
    return IOS27_MACH_OK;
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
static port_set *lookup_set(ios27_mach_v2 *s, uint32_t task,
                            uint32_t name, uint8_t *id) {
    if (!s || !valid_task(task) || !name) return NULL;
    for (size_t i = 0u; i < IOS27_MACH_V2_PORT_SETS; ++i) {
        port_set *set = &s->sets[TASK_INDEX(task)][i];
        if (set->active && set->name == name) {
            if (id) *id = (uint8_t)(i + 1u);
            return set;
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
    if (s) {
        atomic_flag_clear_explicit(&s->guard, memory_order_release);
        s->next_thread_id = 1u;
        for (size_t i = 0u; i < IOS27_MACH_TASK_MAX; ++i)
            s->next_name[i] = FIRST_NAME;
    }
    return s;
}
void ios27_mach_v2_free(ios27_mach_v2 *s) {
    free(s);
}
static int ios27_mach_v2_allocate_unlocked(ios27_mach_v2 *s, uint32_t owner,
                           uint32_t *local_name) {
    if (!s || !valid_task(owner) || !local_name ||
        ios27_mach_v2_validate_external(s, local_name,
                                         sizeof(*local_name)) != IOS27_MACH_OK)
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
/* Port sets are task-local, do not own receive rights, and never
 * grant a right to a different task. All changes are preflighted. */
static int ios27_mach_v2_set_allocate_unlocked(ios27_mach_v2 *s, uint32_t owner,
                               uint32_t *set_name) {
    if (!s || !valid_task(owner) || !set_name ||
        ranges_overlap(set_name, sizeof(*set_name), s, sizeof(*s)))
        return IOS27_MACH_BAD_ARGUMENT;
    if (s->next_name[TASK_INDEX(owner)] == UINT32_MAX)
        return IOS27_MACH_LIMIT;
    for (size_t i = 0u; i < IOS27_MACH_V2_PORT_SETS; ++i) {
        port_set *set = &s->sets[TASK_INDEX(owner)][i];
        if (!set->active) {
            memset(set, 0, sizeof(*set));
            set->name = s->next_name[TASK_INDEX(owner)]++;
            set->active = 1u;
            *set_name = set->name;
            return IOS27_MACH_OK;
        }
    }
    return IOS27_MACH_LIMIT;
}
static int ios27_mach_v2_set_add_unlocked(ios27_mach_v2 *s, uint32_t owner,
                          uint32_t set_name, uint32_t receive_name) {
    uint8_t id = 0u;
    port_set *set = lookup_set(s, owner, set_name, &id);
    local_right *right = receive_right(s, owner, receive_name);
    port_object *obj = object_for(s, right);
    if (!s || !valid_task(owner)) return IOS27_MACH_BAD_ARGUMENT;
    if (!set || !obj || (obj->set_id && obj->set_id != id))
        return IOS27_MACH_BAD_RIGHT;
    obj->set_id = id;
    return IOS27_MACH_OK;
}
static int ios27_mach_v2_set_remove_unlocked(ios27_mach_v2 *s, uint32_t owner,
                             uint32_t set_name, uint32_t receive_name) {
    uint8_t id = 0u;
    port_set *set = lookup_set(s, owner, set_name, &id);
    local_right *right = receive_right(s, owner, receive_name);
    port_object *obj = object_for(s, right);
    if (!s || !valid_task(owner)) return IOS27_MACH_BAD_ARGUMENT;
    if (!set || !obj || obj->set_id != id)
        return IOS27_MACH_BAD_RIGHT;
    obj->set_id = 0u;
    return IOS27_MACH_OK;
}
static int ios27_mach_v2_set_destroy_unlocked(ios27_mach_v2 *s, uint32_t owner,
                              uint32_t set_name) {
    uint8_t id = 0u;
    port_set *set = lookup_set(s, owner, set_name, &id);
    if (!s || !valid_task(owner)) return IOS27_MACH_BAD_ARGUMENT;
    if (!set) return IOS27_MACH_BAD_RIGHT;
    for (size_t i = 0u; i < IOS27_MACH_PORT_MAX; ++i)
        if (s->ports[i].alive && s->ports[i].owner == owner &&
            s->ports[i].set_id == id)
            s->ports[i].set_id = 0u;
    memset(set, 0, sizeof(*set));
    return IOS27_MACH_OK;
}
static int ios27_mach_v2_make_send_unlocked(ios27_mach_v2 *s, uint32_t owner,
                            uint32_t receive_name, uint32_t recipient,
                            uint32_t *recipient_name) {
    local_right *origin, *recipient_right;
    if (!s || !valid_task(owner) || !valid_task(recipient) || !recipient_name ||
        ios27_mach_v2_validate_external(s, recipient_name,
                                      sizeof(*recipient_name)) != IOS27_MACH_OK)
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
static int ios27_mach_v2_drop_send_unlocked(ios27_mach_v2 *s, uint32_t task, uint32_t name) {
    local_right *r = by_name(s, task, name);
    if (!s || !valid_task(task)) return IOS27_MACH_BAD_ARGUMENT;
    if (!r || !r->sends) return IOS27_MACH_BAD_RIGHT;
    --r->sends;
    prune(r);
    return IOS27_MACH_OK;
}
static int ios27_mach_v2_drop_send_once_unlocked(ios27_mach_v2 *s, uint32_t task,
                                 uint32_t name) {
    local_right *r = by_name(s, task, name);
    if (!s || !valid_task(task)) return IOS27_MACH_BAD_ARGUMENT;
    if (!r || !r->once) return IOS27_MACH_BAD_RIGHT;
    --r->once;
    prune(r);
    return IOS27_MACH_OK;
}
/* A bounded fixture event queue, not a kernel notification message. */
static int ios27_mach_v2_watch_dead_name_unlocked(ios27_mach_v2 *s, uint32_t task,
                                  uint32_t local_name) {
    local_right *r;
    if (!s || !valid_task(task)) return IOS27_MACH_BAD_ARGUMENT;
    r = by_name(s, task, local_name);
    if (!r || (!r->sends && !r->once) || !object_for(s, r))
        return IOS27_MACH_BAD_RIGHT;
    r->watch_dead = 1u;
    return IOS27_MACH_OK;
}
static int ios27_mach_v2_poll_dead_name_unlocked(ios27_mach_v2 *s, uint32_t task,
                                 uint32_t *local_name) {
    size_t t;
    if (!s || !valid_task(task) || !local_name ||
        ranges_overlap(local_name, sizeof(*local_name), s, sizeof(*s)))
        return IOS27_MACH_BAD_ARGUMENT;
    t = TASK_INDEX(task);
    if (!s->dead_count[t]) return IOS27_MACH_TIMEOUT;
    *local_name = s->dead_events[t][s->dead_head[t]];
    s->dead_events[t][s->dead_head[t]] = 0u;
    s->dead_head[t] =
        (uint8_t)((s->dead_head[t] + 1u) % IOS27_MACH_V2_NAMES);
    --s->dead_count[t];
    return IOS27_MACH_OK;
}
static void enqueue_dead_name(ios27_mach_v2 *s, uint32_t task,
                              uint32_t name) {
    size_t t = TASK_INDEX(task);
    size_t tail = (s->dead_head[t] + s->dead_count[t]) %
                  IOS27_MACH_V2_NAMES;
    s->dead_events[t][tail] = name;
    ++s->dead_count[t];
}
static int ios27_mach_v2_rights_unlocked(const ios27_mach_v2 *s, uint32_t task,
                         uint32_t name, uint32_t *send, uint32_t *send_once,
                         unsigned *receive) {
    const local_right *r = by_name_const(s, task, name);
    if (!s || !valid_task(task) || !send || !send_once || !receive ||
        ios27_mach_v2_validate_external(s, send, sizeof(*send)) != IOS27_MACH_OK ||
        ios27_mach_v2_validate_external(s, send_once,
                                         sizeof(*send_once)) != IOS27_MACH_OK ||
        ios27_mach_v2_validate_external(s, receive,
                                         sizeof(*receive)) != IOS27_MACH_OK ||
        ranges_overlap(send, sizeof(*send), send_once, sizeof(*send_once)) ||
        ranges_overlap(send, sizeof(*send), receive, sizeof(*receive)) ||
        ranges_overlap(send_once, sizeof(*send_once),
                       receive, sizeof(*receive)))
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
static int ios27_mach_v2_destroy_unlocked(ios27_mach_v2 *s, uint32_t owner,
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
        if (r && r->watch_dead &&
            s->dead_count[TASK_INDEX(task)] == IOS27_MACH_V2_NAMES)
            return IOS27_MACH_LIMIT;
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
        if (r) {
            if (r->watch_dead) enqueue_dead_name(s, task, r->name);
            memset(r, 0, sizeof(*r));
        }
    }
    return IOS27_MACH_OK;
}
/*
 * Transactional synthetic task cleanup. Cancel only grants in queues
 * owned by the retiring task; any grant waiting in a foreign live queue
 * must keep teardown from silently invalidating its destination credit.
 * This does not model XNU dead-name/notification behavior.
 */
static int ios27_mach_v2_task_teardown_unlocked(ios27_mach_v2 *s, uint32_t task) {
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
    /* Preflight event capacity across all affected namespaces. */
    for (uint32_t other = 1u; other <= IOS27_MACH_TASK_MAX; ++other) {
        uint32_t needed = 0u;
        if (other == task) continue;
        for (size_t i = 0u; i < IOS27_MACH_PORT_MAX; ++i) {
            const local_right *r = by_object(s, other, (uint8_t)(i + 1u));
            if (owned[i] && r && r->watch_dead) ++needed;
        }
        if ((uint32_t)s->dead_count[TASK_INDEX(other)] + needed >
            IOS27_MACH_V2_NAMES)
            return IOS27_MACH_LIMIT;
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
            if (owner != task && r->object && owned[r->object - 1u] &&
                r->watch_dead)
                enqueue_dead_name(s, owner, r->name);
            if ((owner == task) ||
                (r->object && owned[r->object - 1u]))
                memset(r, 0, sizeof(*r));
        }
    }
    memset(s->sets[TASK_INDEX(task)], 0,
           sizeof(s->sets[TASK_INDEX(task)]));
    memset(s->dead_events[TASK_INDEX(task)], 0,
           sizeof(s->dead_events[TASK_INDEX(task)]));
    s->dead_head[TASK_INDEX(task)] = 0u;
    s->dead_count[TASK_INDEX(task)] = 0u;
    memset(s->threads[TASK_INDEX(task)], 0,
           sizeof(s->threads[TASK_INDEX(task)]));
    memset(s->exception_names[TASK_INDEX(task)], 0,
           sizeof(s->exception_names[TASK_INDEX(task)]));
    return IOS27_MACH_OK;
}


static int send_impl(ios27_mach_v2 *s, uint32_t task,
                     const void *wire, size_t size, uint32_t timeout_ms,
                     const uint8_t *arena, size_t arena_size) {
    const uint8_t *p = wire;
    uint32_t bits, dest_name, reply_name, remote_disposition, local_disposition;
    local_right *dest_right, *reply_right = NULL;
    local_right *descriptors[IOS27_MACH_V2_DESCRIPTORS] = {0};
    uint8_t desc_objects[IOS27_MACH_V2_DESCRIPTORS] = {0};
    uint8_t desc_moves[IOS27_MACH_V2_DESCRIPTORS] = {0};
    uint16_t desc_offsets[IOS27_MACH_V2_DESCRIPTORS] = {0};
    uint8_t ool_bytes[IOS27_MACH_OOL_MAX] = {0};
    uint16_t ool_size = 0u, ool_offset = 0u;
    uint8_t ool_present = 0u;
    port_object *dest;
    uint8_t reply_obj = 0u, reply_move = 0u;
    uint32_t receiver, count = 0u, port_count = 0u;
    size_t tail;
    unsigned additional = 0u;
    (void)timeout_ms;
    if (!s || !valid_task(task) || !p || size < 24u ||
        size > IOS27_MACH_MSG_MAX || (size & 3u) ||
        ranges_overlap(p, size, s, sizeof(*s)) ||
        (!arena && arena_size) ||
        (arena && ios27_mach_v2_validate_external(s, arena, arena_size) !=
                      IOS27_MACH_OK))
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
        size_t offset = 28u;
        for (uint32_t i = 0u; i < count; ++i) {
            uint8_t type;
            if (offset > size || size - offset < 12u)
                return IOS27_MACH_UNSUPPORTED;
            type = p[offset + 11u];
            if (type == IOS27_MACH_OOL_DESCRIPTOR) {
                uint64_t guest_address;
                uint32_t guest_length;
                if (!arena || ool_present || size - offset < 16u ||
                    p[offset + 8u] != 0u || p[offset + 9u] > 1u ||
                    p[offset + 10u] != 0u)
                    return IOS27_MACH_UNSUPPORTED;
                guest_address = rd64(p + offset);
                guest_length = rd32(p + offset + 12u);
                if (guest_length > IOS27_MACH_OOL_MAX ||
                    guest_address > (uint64_t)arena_size ||
                    guest_length > (uint64_t)arena_size - guest_address)
                    return IOS27_MACH_BAD_ARGUMENT;
                if (guest_length)
                    memcpy(ool_bytes, arena + (size_t)guest_address,
                           guest_length);
                ool_present = 1u;
                ool_offset = (uint16_t)offset;
                ool_size = (uint16_t)guest_length;
                offset += 16u;
                continue;
            }
            if (type != IOS27_MACH_PORT_DESCRIPTOR)
                return IOS27_MACH_UNSUPPORTED;
            uint8_t disposition = p[offset + 10u];
            if (rd32(p + offset + 4u) || rd16(p + offset + 8u) ||
                (disposition != IOS27_MACH_COPY_SEND &&
                 disposition != IOS27_MACH_MOVE_SEND))
                return IOS27_MACH_UNSUPPORTED;
            descriptors[port_count] = by_name(s, task, rd32(p + offset));
            if (!object_for(s, descriptors[port_count]) ||
                !descriptors[port_count]->sends)
                return IOS27_MACH_BAD_RIGHT;
            desc_objects[port_count] = descriptors[port_count]->object;
            desc_moves[port_count] =
                (uint8_t)(disposition == IOS27_MACH_MOVE_SEND);
            desc_offsets[port_count] = (uint16_t)offset;
            ++port_count;
            offset += 12u;
        }
    }

    /* Every source debit is counted before mutating any rights. In
     * particular, the destination and several descriptors may name the
     * same send right. A single uref cannot satisfy repeated MOVE_SEND. */
    for (uint32_t i = 0u; i < port_count; ++i) {
        uint32_t needed = 0u;
        if (!desc_moves[i]) continue;
        for (uint32_t j = 0u; j < port_count; ++j)
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
    for (uint32_t i = 0u; i < port_count; ++i) {
        uint32_t arriving = 0u;
        local_right *r = by_object(s, receiver, desc_objects[i]);
        for (uint32_t j = 0u; j < port_count; ++j)
            if (desc_objects[j] == desc_objects[i]) ++arriving;
        if (r && (uint32_t)r->sends + r->pending_sends + arriving >
                 IOS27_MACH_RIGHT_MAX)
            return IOS27_MACH_LIMIT;
    }
    /* Reserve one local name per *distinct* object, whether the new
     * credits are send, send-once, or both. */
    if (reply_obj && !by_object(s, receiver, reply_obj)) ++additional;
    for (uint32_t i = 0u; i < port_count; ++i) {
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
    for (uint32_t i = 0u; i < port_count; ++i)
        ++reserve_name(s, receiver, desc_objects[i])->pending_sends;
    if (reply_move) --reply_right->once;
    for (uint32_t i = 0u; i < port_count; ++i)
        if (desc_moves[i]) --descriptors[i]->sends;
    if (remote_disposition == IOS27_MACH_MOVE_SEND)
        --dest_right->sends;
    else if (remote_disposition == IOS27_MACH_MOVE_SEND_ONCE)
        --dest_right->once;
    if (reply_move) prune(reply_right);
    for (uint32_t i = 0u; i < port_count; ++i)
        if (desc_moves[i]) prune(descriptors[i]);
    prune(dest_right);

    tail = (dest->head + dest->count) % IOS27_MACH_QUEUE_MAX;
    queued_wire *slot = &dest->queue[tail];
    memset(slot, 0, sizeof(*slot));
    memcpy(slot->bytes, p, size);
    slot->length = (uint16_t)size;
    slot->reply_object = reply_obj;
    slot->descriptor_count = (uint8_t)port_count;
    memcpy(slot->descriptor_objects, desc_objects, port_count);
    memcpy(slot->descriptor_offsets, desc_offsets,
           port_count * sizeof(desc_offsets[0]));
    slot->ool_present = ool_present;
    slot->ool_offset = ool_offset;
    slot->ool_size = ool_size;
    if (ool_present && ool_size)
        memcpy(slot->ool_bytes, ool_bytes, ool_size);
    ++dest->count;
    return IOS27_MACH_OK;
}
static int ios27_mach_v2_send_unlocked(ios27_mach_v2 *s, uint32_t task,
                       const void *wire, size_t size, uint32_t timeout_ms) {
    return send_impl(s, task, wire, size, timeout_ms, NULL, 0u);
}
static int ios27_mach_v2_send_guest_ool_unlocked(ios27_mach_v2 *s, uint32_t task,
                                 const void *wire, size_t size,
                                 uint32_t timeout_ms,
                                 const uint8_t *arena, size_t arena_size) {
    return send_impl(s, task, wire, size, timeout_ms, arena, arena_size);
}
static int receive_impl(ios27_mach_v2 *s, uint32_t task,
                        uint32_t receive_name, void *out,
                        size_t *inout_size, uint32_t timeout_ms,
                        int allow_ool, uint64_t guest_offset) {
    local_right *right;
    port_object *obj;
    queued_wire *msg;
    local_right *reply;
    local_right *descriptors[IOS27_MACH_V2_DESCRIPTORS] = {0};
    uint8_t *output = out;
    size_t required;
    (void)timeout_ms;
    if (!s || !valid_task(task) || !out || !inout_size ||
        ranges_overlap(inout_size, sizeof(*inout_size), s, sizeof(*s)))
        return IOS27_MACH_BAD_ARGUMENT;
    right = receive_right(s, task, receive_name);
    obj = object_for(s, right);
    if (!obj) return IOS27_MACH_BAD_RIGHT;
    if (!obj->count) return IOS27_MACH_TIMEOUT;
    msg = &obj->queue[obj->head];
    if (msg->ool_present && !allow_ool)
        return IOS27_MACH_UNSUPPORTED; /* preserve queue for OOL-aware API */
    required = msg->length + (msg->ool_present ? msg->ool_size : 0u);
    if (msg->ool_present && guest_offset > UINT64_MAX - msg->length)
        return IOS27_MACH_BAD_ARGUMENT;
    if (ranges_overlap(output, required, s, sizeof(*s)) ||
        ranges_overlap(output, required,
                       inout_size, sizeof(*inout_size)))
        return IOS27_MACH_BAD_ARGUMENT;
    if (*inout_size < required) {
        *inout_size = required;
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
        size_t offset = msg->descriptor_offsets[i];
        wr32(output + offset, descriptors[i]->name);
        output[offset + 10u] = IOS27_MACH_MOVE_SEND;
        --descriptors[i]->pending_sends;
        ++descriptors[i]->sends;
    }
    if (reply) {
        --reply->pending_once;
        ++reply->once;
    }
    if (msg->ool_present) {
        wr64(output + msg->ool_offset, guest_offset + msg->length);
        if (msg->ool_size)
            memcpy(output + msg->length, msg->ool_bytes, msg->ool_size);
    }
    *inout_size = required;
    memset(msg, 0, sizeof(*msg));
    obj->head = (uint8_t)((obj->head + 1u) % IOS27_MACH_QUEUE_MAX);
    --obj->count;
    ++obj->seqno;
    return IOS27_MACH_OK;
}

static int ios27_mach_v2_receive_unlocked(ios27_mach_v2 *s, uint32_t task,
                          uint32_t receive_name, void *out,
                          size_t *inout_size, uint32_t timeout_ms) {
    return receive_impl(s, task, receive_name, out, inout_size,
                        timeout_ms, 0, 0u);
}
static int ios27_mach_v2_receive_guest_ool_unlocked(ios27_mach_v2 *s, uint32_t task,
                                    uint32_t receive_name, void *out,
                                    size_t *inout_size, uint32_t timeout_ms,
                                    uint64_t guest_offset) {
    return receive_impl(s, task, receive_name, out, inout_size,
                        timeout_ms, 1, guest_offset);
}
/* No set receive can mutate the selected port, rights, or cursor before
 * the underlying direct receive has committed successfully. */
static int ios27_mach_v2_receive_set_unlocked(ios27_mach_v2 *s, uint32_t task,
                              uint32_t set_name, void *out,
                              size_t *inout_size, uint32_t timeout_ms) {
    port_set *set;
    uint8_t id = 0u;
    if (!s || !valid_task(task) || !out || !inout_size ||
        ranges_overlap(inout_size, sizeof(*inout_size), s, sizeof(*s)))
        return IOS27_MACH_BAD_ARGUMENT;
    set = lookup_set(s, task, set_name, &id);
    if (!set) return IOS27_MACH_BAD_RIGHT;
    for (size_t i = 0u; i < IOS27_MACH_PORT_MAX; ++i) {
        size_t index = (set->cursor + i) % IOS27_MACH_PORT_MAX;
        port_object *obj = &s->ports[index];
        local_right *right;
        int status;
        if (!obj->alive || obj->owner != task || obj->set_id != id ||
            !obj->count)
            continue;
        right = by_object(s, task, (uint8_t)(index + 1u));
        if (!right || !right->receive) return IOS27_MACH_BAD_RIGHT;
        status = ios27_mach_v2_receive_unlocked(s, task, right->name,
                                       out, inout_size, timeout_ms);
        if (status == IOS27_MACH_OK)
            set->cursor = (uint8_t)((index + 1u) % IOS27_MACH_PORT_MAX);
        return status;
    }
    return IOS27_MACH_TIMEOUT;
}

static int ios27_mach_v2_receive_trailer_unlocked(ios27_mach_v2 *s, uint32_t task,
                                  uint32_t receive_name, void *out,
                                  size_t *inout_size, uint32_t timeout_ms) {
    local_right *right;
    port_object *obj;
    size_t wire_len, needed, wire_capacity;
    uint32_t sequence;
    uint8_t *bytes = out;
    int status;
    if (!s || !valid_task(task) || !out || !inout_size ||
        ranges_overlap(inout_size, sizeof(*inout_size), s, sizeof(*s)))
        return IOS27_MACH_BAD_ARGUMENT;
    right = receive_right(s, task, receive_name);
    obj = object_for(s, right);
    if (!obj) return IOS27_MACH_BAD_RIGHT;
    if (!obj->count) return IOS27_MACH_TIMEOUT;
    wire_len = obj->queue[obj->head].length;
    needed = wire_len + IOS27_MACH_SEQNO_TRAILER_SIZE;
    if (ranges_overlap(out, needed, s, sizeof(*s)) ||
        ranges_overlap(out, needed, inout_size, sizeof(*inout_size)))
        return IOS27_MACH_BAD_ARGUMENT;
    if (*inout_size < needed) {
        *inout_size = needed;
        return IOS27_MACH_TOO_LARGE;
    }
    sequence = obj->seqno;
    wire_capacity = *inout_size;
    status = ios27_mach_v2_receive_unlocked(s, task, receive_name, out,
                                   &wire_capacity, timeout_ms);
    if (status != IOS27_MACH_OK) return status;
    wr32(bytes + wire_len, 0u); /* MACH_MSG_TRAILER_FORMAT_0 */
    wr32(bytes + wire_len + 4u, IOS27_MACH_SEQNO_TRAILER_SIZE);
    wr32(bytes + wire_len + 8u, sequence);
    *inout_size = needed;
    return IOS27_MACH_OK;
}

/* Phase 19c: independently owned, never-recycled fixture thread identities. */
static const thread_record *thread_lookup(const ios27_mach_v2 *s,
                                           uint32_t task, uint32_t id) {
    if (!s || !valid_task(task) || !id) return NULL;
    for (size_t i = 0u; i < IOS27_MACH_V2_THREADS; ++i) {
        const thread_record *r = &s->threads[TASK_INDEX(task)][i];
        if (r->active && r->id == id) return r;
    }
    return NULL;
}

static int ios27_mach_v2_thread_create_unlocked(ios27_mach_v2 *s, uint32_t task,
                                uint32_t *thread_id) {
    if (!s || !valid_task(task) || !thread_id ||
        ios27_mach_v2_validate_external(s, thread_id,
                                         sizeof(*thread_id)) != IOS27_MACH_OK)
        return IOS27_MACH_BAD_ARGUMENT;
    if (s->next_thread_id == UINT32_MAX) return IOS27_MACH_LIMIT;
    for (size_t i = 0u; i < IOS27_MACH_V2_THREADS; ++i) {
        thread_record *r = &s->threads[TASK_INDEX(task)][i];
        if (!r->active) {
            r->id = s->next_thread_id++;
            r->active = 1u;
            *thread_id = r->id;
            return IOS27_MACH_OK;
        }
    }
    return IOS27_MACH_LIMIT;
}

static int ios27_mach_v2_thread_destroy_unlocked(ios27_mach_v2 *s, uint32_t task,
                                 uint32_t thread_id) {
    if (!s || !valid_task(task)) return IOS27_MACH_BAD_ARGUMENT;
    for (size_t i = 0u; i < IOS27_MACH_V2_THREADS; ++i) {
        thread_record *r = &s->threads[TASK_INDEX(task)][i];
        if (r->active && r->id == thread_id) {
            memset(r, 0, sizeof(*r));
            return IOS27_MACH_OK;
        }
    }
    return IOS27_MACH_BAD_RIGHT;
}

static int ios27_mach_v2_thread_identity_unlocked(const ios27_mach_v2 *s, uint32_t task,
                                  uint32_t thread_id, uint32_t *owner) {
    if (!s || !valid_task(task) || !owner ||
        ios27_mach_v2_validate_external(s, owner,
                                         sizeof(*owner)) != IOS27_MACH_OK)
        return IOS27_MACH_BAD_ARGUMENT;
    if (!thread_lookup(s, task, thread_id)) return IOS27_MACH_BAD_RIGHT;
    *owner = task;
    return IOS27_MACH_OK;
}

/* No ownership transfer is implicit: a registered name remains a send right
 * in the registering task's local namespace. Stale names never get reused. */
static uint32_t live_exception_name(const ios27_mach_v2 *s,
                                     uint32_t task, uint32_t slot) {
    uint32_t name = s->exception_names[TASK_INDEX(task)][slot];
    const local_right *r = by_name_const(s, task, name);
    if (!r || !r->sends || !r->object ||
        r->object > IOS27_MACH_PORT_MAX ||
        !s->ports[r->object - 1u].alive)
        return 0u;
    return name;
}

static int ios27_mach_v2_exception_set_unlocked(ios27_mach_v2 *s, uint32_t task,
                                uint32_t mask, uint32_t send_name) {
    const uint32_t supported = (UINT32_C(1) << IOS27_MACH_V2_EXCEPTION_SLOTS) - 1u;
    if (!s || !valid_task(task) || !mask || (mask & ~supported))
        return IOS27_MACH_BAD_ARGUMENT;
    if (send_name) {
        const local_right *r = by_name_const(s, task, send_name);
        if (!r || !r->sends || !r->object ||
            r->object > IOS27_MACH_PORT_MAX ||
            !s->ports[r->object - 1u].alive)
            return IOS27_MACH_BAD_RIGHT;
    }
    for (uint32_t bit = 0u; bit < IOS27_MACH_V2_EXCEPTION_SLOTS; ++bit)
        if (mask & (UINT32_C(1) << bit))
            s->exception_names[TASK_INDEX(task)][bit] = send_name;
    return IOS27_MACH_OK;
}

static int ios27_mach_v2_exception_get_unlocked(const ios27_mach_v2 *s, uint32_t task,
                                uint32_t slot, uint32_t *send_name) {
    if (!s || !valid_task(task) || slot >= IOS27_MACH_V2_EXCEPTION_SLOTS ||
        !send_name ||
        ios27_mach_v2_validate_external(s, send_name,
                                         sizeof(*send_name)) != IOS27_MACH_OK)
        return IOS27_MACH_BAD_ARGUMENT;
    *send_name = live_exception_name(s, task, slot);
    return IOS27_MACH_OK;
}

static int ios27_mach_v2_exception_raise_unlocked(ios27_mach_v2 *s, uint32_t task,
                                  uint32_t thread_id, uint32_t slot,
                                  uint32_t code) {
    uint8_t wire[40] = {0};
    uint32_t name;
    if (!s || !valid_task(task) || slot >= IOS27_MACH_V2_EXCEPTION_SLOTS)
        return IOS27_MACH_BAD_ARGUMENT;
    if (!thread_lookup(s, task, thread_id)) return IOS27_MACH_BAD_RIGHT;
    name = live_exception_name(s, task, slot);
    if (!name) return IOS27_MACH_BAD_RIGHT;
    wr32(wire, IOS27_MACH_COPY_SEND);
    wr32(wire + 4u, sizeof(wire));
    wr32(wire + 8u, name);
    wr32(wire + 20u, IOS27_MACH_V2_EXCEPTION_MSG_ID);
    wr32(wire + 24u, thread_id);
    wr32(wire + 28u, task);
    wr32(wire + 32u, slot);
    wr32(wire + 36u, code);
    return ios27_mach_v2_send_unlocked(s, task, wire, sizeof(wire), 0u);
}

/*
 * Serialize shared Mach fixture mutations across host threads. Operations
 * never sleep on a guest queue; only this short process-local state lock
 * yields while another host worker owns it. Callers still own lifetime and
 * must not free the fixture or race writes to their external guest arenas.
 * Internal unlocked entrypoints are shared by compound receive/exception
 * paths so nested API calls cannot deadlock.
 */
static void ios27_mach_v2_lock(ios27_mach_v2 *s) {
    while (atomic_flag_test_and_set_explicit(&s->guard, memory_order_acquire))
        (void)sched_yield();
}
static void ios27_mach_v2_unlock(ios27_mach_v2 *s) {
    atomic_flag_clear_explicit(&s->guard, memory_order_release);
}

int ios27_mach_v2_allocate(ios27_mach_v2 *s, uint32_t owner,
                           uint32_t *local_name) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_allocate_unlocked(s, owner, local_name);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_set_allocate(ios27_mach_v2 *s, uint32_t owner,
                               uint32_t *set_name) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_set_allocate_unlocked(s, owner, set_name);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_set_add(ios27_mach_v2 *s, uint32_t owner,
                          uint32_t set_name, uint32_t receive_name) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_set_add_unlocked(s, owner, set_name, receive_name);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_set_remove(ios27_mach_v2 *s, uint32_t owner,
                             uint32_t set_name, uint32_t receive_name) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_set_remove_unlocked(s, owner, set_name, receive_name);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_set_destroy(ios27_mach_v2 *s, uint32_t owner,
                              uint32_t set_name) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_set_destroy_unlocked(s, owner, set_name);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_make_send(ios27_mach_v2 *s, uint32_t owner,
                            uint32_t receive_name, uint32_t recipient,
                            uint32_t *recipient_name) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_make_send_unlocked(s, owner, receive_name, recipient, recipient_name);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_drop_send(ios27_mach_v2 *s, uint32_t task, uint32_t name) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_drop_send_unlocked(s, task, name);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_drop_send_once(ios27_mach_v2 *s, uint32_t task,
                                 uint32_t name) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_drop_send_once_unlocked(s, task, name);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_watch_dead_name(ios27_mach_v2 *s, uint32_t task,
                                  uint32_t local_name) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_watch_dead_name_unlocked(s, task, local_name);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_poll_dead_name(ios27_mach_v2 *s, uint32_t task,
                                 uint32_t *local_name) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_poll_dead_name_unlocked(s, task, local_name);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_rights(const ios27_mach_v2 *s, uint32_t task,
                         uint32_t name, uint32_t *send, uint32_t *send_once,
                         unsigned *receive) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_rights_unlocked(s, task, name, send, send_once, receive);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_destroy(ios27_mach_v2 *s, uint32_t owner,
                          uint32_t receive_name) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_destroy_unlocked(s, owner, receive_name);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_task_teardown(ios27_mach_v2 *s, uint32_t task) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_task_teardown_unlocked(s, task);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_send(ios27_mach_v2 *s, uint32_t task,
                       const void *wire, size_t size, uint32_t timeout_ms) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_send_unlocked(s, task, wire, size, timeout_ms);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_send_guest_ool(ios27_mach_v2 *s, uint32_t task,
                                 const void *wire, size_t size,
                                 uint32_t timeout_ms,
                                 const uint8_t *arena, size_t arena_size) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_send_guest_ool_unlocked(s, task, wire, size, timeout_ms, arena, arena_size);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_receive(ios27_mach_v2 *s, uint32_t task,
                          uint32_t receive_name, void *out,
                          size_t *inout_size, uint32_t timeout_ms) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_receive_unlocked(s, task, receive_name, out, inout_size, timeout_ms);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_receive_guest_ool(ios27_mach_v2 *s, uint32_t task,
                                    uint32_t receive_name, void *out,
                                    size_t *inout_size, uint32_t timeout_ms,
                                    uint64_t guest_offset) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_receive_guest_ool_unlocked(s, task, receive_name, out, inout_size, timeout_ms, guest_offset);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_receive_set(ios27_mach_v2 *s, uint32_t task,
                              uint32_t set_name, void *out,
                              size_t *inout_size, uint32_t timeout_ms) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_receive_set_unlocked(s, task, set_name, out, inout_size, timeout_ms);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_receive_trailer(ios27_mach_v2 *s, uint32_t task,
                                  uint32_t receive_name, void *out,
                                  size_t *inout_size, uint32_t timeout_ms) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_receive_trailer_unlocked(s, task, receive_name, out, inout_size, timeout_ms);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_thread_create(ios27_mach_v2 *s, uint32_t task,
                                uint32_t *thread_id) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_thread_create_unlocked(s, task, thread_id);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_thread_destroy(ios27_mach_v2 *s, uint32_t task,
                                 uint32_t thread_id) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_thread_destroy_unlocked(s, task, thread_id);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_thread_identity(const ios27_mach_v2 *s, uint32_t task,
                                  uint32_t thread_id, uint32_t *owner) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_thread_identity_unlocked(s, task, thread_id, owner);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_exception_set(ios27_mach_v2 *s, uint32_t task,
                                uint32_t mask, uint32_t send_name) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_exception_set_unlocked(s, task, mask, send_name);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_exception_get(const ios27_mach_v2 *s, uint32_t task,
                                uint32_t slot, uint32_t *send_name) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_exception_get_unlocked(s, task, slot, send_name);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}

int ios27_mach_v2_exception_raise(ios27_mach_v2 *s, uint32_t task,
                                  uint32_t thread_id, uint32_t slot,
                                  uint32_t code) {
    int result;
    if (!s) return IOS27_MACH_BAD_ARGUMENT;
    ios27_mach_v2_lock((ios27_mach_v2 *)(void *)s);
    result = ios27_mach_v2_exception_raise_unlocked(s, task, thread_id, slot, code);
    ios27_mach_v2_unlock((ios27_mach_v2 *)(void *)s);
    return result;
}
