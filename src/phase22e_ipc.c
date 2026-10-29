#define _POSIX_C_SOURCE 200809L
#include "ios27/phase22e_ipc.h"
#include <errno.h>
#include <pthread.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

_Static_assert(sizeof(phase22e_header) == 24, "mach-shaped header must be 24 bytes");
#define P22_REFS 65535u
#define P22_OOL_BUDGET (PHASE22E_QUEUE * PHASE22E_OOL_MAX * 4u)

typedef struct p22_waiter {
    struct p22_waiter *next;
} p22_waiter;
typedef struct {
    uint8_t wire[PHASE22E_WIRE_MAX];
    uint8_t ool[PHASE22E_OOL_MAX];
    size_t wire_size, ool_size;
    uint32_t grants[PHASE22E_DESCRIPTORS + 1];
    unsigned grant_count;
    uint32_t reply;
    uint8_t reply_mode;
} message;
typedef struct {
    uint32_t owner;
    uint8_t live, retired;
    uint32_t inflight;
    unsigned first, used;
    p22_waiter *send_waiters, *recv_waiters;
    message queue[PHASE22E_QUEUE];
} port_state;
typedef struct {
    phase22e_method methods[PHASE22E_METHODS];
    unsigned count, active;
    uint32_t owner;
} service;
struct phase22e_space {
    pthread_mutex_t mu;
    pthread_cond_t changed;
    uint32_t last_port;
    uint64_t cancel_generation;
    uint8_t alive[PHASE22E_TASKS + 1];
    port_state ports[PHASE22E_PORTS + 1];
    service services[PHASE22E_PORTS + 1];
    uint32_t send[PHASE22E_TASKS + 1][PHASE22E_PORTS + 1];
    uint32_t once[PHASE22E_TASKS + 1][PHASE22E_PORTS + 1];
    uint32_t reserved_send[PHASE22E_TASKS + 1][PHASE22E_PORTS + 1];
    uint32_t reserved_once[PHASE22E_TASKS + 1][PHASE22E_PORTS + 1];
    phase22e_stats stats;
};
/* Linked FIFO of stack-allocated wait nodes, always managed under mu. */
static void join_waiters(p22_waiter **head, p22_waiter *node) {
    p22_waiter **tail = head;
    while (*tail) tail = &(*tail)->next;
    *tail = node;
}
static void leave_waiters(p22_waiter **head, p22_waiter *node) {
    while (*head && *head != node) head = &(*head)->next;
    if (*head == node) *head = node->next;
    node->next = NULL;
}
static int valid_task(const phase22e_space *s, uint32_t t) {
    return s && t && t <= PHASE22E_TASKS && s->alive[t];
}
static int valid_port(const phase22e_space *s, uint32_t p) {
    return p && p <= s->last_port && s->ports[p].live;
}
static void mark_rejected(phase22e_space *s, int rc) {
    if (rc == PHASE22E_OK) return;
    ++s->stats.rejected;
    if (rc == PHASE22E_TIMEOUT) ++s->stats.timeouts;
    else if (rc == PHASE22E_BAD || rc == PHASE22E_SIZE ||
             rc == PHASE22E_UNSUPPORTED) ++s->stats.bad_messages;
    else if (rc == PHASE22E_RIGHT || rc == PHASE22E_DEAD)
        ++s->stats.bad_rights;
    else if (rc == PHASE22E_QUOTA) ++s->stats.quota_events;
    else if (rc == PHASE22E_CANCELLED) ++s->stats.cancellations;
}
static int preflight_fail(phase22e_space *s, int rc) {
    if (s) {
        pthread_mutex_lock(&s->mu);
        mark_rejected(s, rc);
        pthread_mutex_unlock(&s->mu);
    }
    return rc;
}
static int overlap(const void *a, size_t na, const void *b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    if (!na || !nb) return 0;
    if (!a || !b || na > UINTPTR_MAX - x || nb > UINTPTR_MAX - y) return 1;
    return x < y + nb && y < x + na;
}
static int external(const phase22e_space *s, const void *p, size_t n) {
    return (!n || p) && !overlap(s, sizeof(*s), p, n) &&
           (!n || n <= UINTPTR_MAX - (uintptr_t)p);
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t le64(const uint8_t *p) {
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}
static void wr32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (i * 8));
}
static void deadline(struct timespec *ts, uint32_t ms) {
    (void)clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += (time_t)(ms / 1000u);
    ts->tv_nsec += (long)(ms % 1000u) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) { ++ts->tv_sec; ts->tv_nsec -= 1000000000L; }
}
static int wait_changed(phase22e_space *s, uint64_t gen,
                        const struct timespec *until) {
    int err;
    ++s->stats.waiters;
    if (s->stats.waiters > s->stats.peak_waiters)
        s->stats.peak_waiters = s->stats.waiters;
    err = pthread_cond_timedwait(&s->changed, &s->mu, until);
    --s->stats.waiters;
    if (s->cancel_generation != gen) return PHASE22E_CANCELLED;
    if (err == ETIMEDOUT) return PHASE22E_TIMEOUT;
    return err == 0 ? PHASE22E_OK : PHASE22E_BAD;
}
phase22e_space *phase22e_new(void) {
    phase22e_space *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    if (pthread_mutex_init(&s->mu, NULL)) { free(s); return NULL; }
    if (pthread_cond_init(&s->changed, NULL)) {
        (void)pthread_mutex_destroy(&s->mu); free(s); return NULL;
    }
    for (uint32_t i = 1; i <= PHASE22E_TASKS; ++i) s->alive[i] = 1;
    return s;
}
void phase22e_free(phase22e_space *s) {
    if (!s) return;
    /* Caller joins all threads before freeing the space. */
    (void)pthread_cond_destroy(&s->changed);
    (void)pthread_mutex_destroy(&s->mu);
    free(s);
}
int phase22e_port_create(phase22e_space *s, uint32_t t, uint32_t *out) {
    if (!s || !out || !external(s, out, sizeof(*out))) return PHASE22E_BAD;
    pthread_mutex_lock(&s->mu);
    int rc = PHASE22E_RIGHT;
    if (!valid_task(s, t)) goto end;
    if (s->last_port == PHASE22E_PORTS) { rc = PHASE22E_QUOTA; goto end; }
    uint32_t p = ++s->last_port;
    s->ports[p].owner = t;
    s->ports[p].live = 1;
    ++s->stats.live_ports;
    *out = p;
    rc = PHASE22E_OK;
end:
    pthread_mutex_unlock(&s->mu); return rc;
}
int phase22e_make_send(phase22e_space *s, uint32_t owner, uint32_t p, uint32_t to) {
    if (!s) return PHASE22E_BAD;
    pthread_mutex_lock(&s->mu);
    int rc = PHASE22E_RIGHT;
    if (valid_task(s, owner) && valid_task(s, to) && valid_port(s, p) &&
        s->ports[p].owner == owner) {
        if (s->send[to][p] + s->reserved_send[to][p] == P22_REFS) rc = PHASE22E_QUOTA;
        else { ++s->send[to][p]; rc = PHASE22E_OK; }
    }
    pthread_mutex_unlock(&s->mu); return rc;
}
int phase22e_drop_send(phase22e_space *s, uint32_t t, uint32_t p) {
    if (!s) return PHASE22E_BAD;
    pthread_mutex_lock(&s->mu);
    int rc = PHASE22E_RIGHT;
    if (valid_task(s, t) && p && p <= s->last_port && s->send[t][p]) {
        --s->send[t][p]; rc = PHASE22E_OK;
    }
    pthread_mutex_unlock(&s->mu); return rc;
}
static void retire_port(phase22e_space *s, uint32_t p) {
    port_state *ps = &s->ports[p];
    ps->live = 0; ps->retired = 1;
    memset(&s->services[p], 0, sizeof(s->services[p]));
    --s->stats.live_ports; ++s->stats.dead_ports;
    for (uint32_t t = 1; t <= PHASE22E_TASKS; ++t) {
        s->send[t][p] = 0; s->once[t][p] = 0;
        s->reserved_send[t][p] = 0; s->reserved_once[t][p] = 0;
    }
    for (unsigned i = 0; i < ps->used; ++i) {
        message *m = &ps->queue[(ps->first + i) % PHASE22E_QUEUE];
        for (unsigned j = 0; j < m->grant_count; ++j) {
            uint32_t a = m->grants[j];
            if (s->reserved_send[ps->owner][a])
                --s->reserved_send[ps->owner][a];
            --s->ports[a].inflight;
            --s->stats.in_flight_transfers;
        }
        if (m->reply_mode) {
            if (s->reserved_once[ps->owner][m->reply])
                --s->reserved_once[ps->owner][m->reply];
            --s->ports[m->reply].inflight;
            --s->stats.in_flight_transfers;
        }
        s->stats.ool_bytes_pending -= m->ool_size;
        --s->stats.messages_pending;
        memset(m, 0, sizeof(*m));
    }
    ps->first = 0; ps->used = 0;
}
int phase22e_destroy(phase22e_space *s, uint32_t t, uint32_t p) {
    if (!s) return PHASE22E_BAD;
    pthread_mutex_lock(&s->mu);
    int rc = PHASE22E_RIGHT;
    if (valid_task(s, t) && valid_port(s, p) && s->ports[p].owner == t) {
        if (s->ports[p].inflight || s->services[p].active) rc = PHASE22E_QUOTA;
        else { retire_port(s, p); pthread_cond_broadcast(&s->changed); rc = PHASE22E_OK; }
    }
    pthread_mutex_unlock(&s->mu); return rc;
}
int phase22e_task_die(phase22e_space *s, uint32_t t) {
    if (!s) return PHASE22E_BAD;
    pthread_mutex_lock(&s->mu);
    int rc = PHASE22E_RIGHT;
    if (!valid_task(s, t)) goto end;
    /* Atomic preflight: other queues may reference ports owned by this task. */
    for (uint32_t p = 1; p <= s->last_port; ++p)
        if (s->ports[p].live && s->ports[p].owner == t &&
            (s->ports[p].inflight || s->services[p].active)) {
            rc = PHASE22E_QUOTA; goto end;
        }
    for (uint32_t p = 1; p <= s->last_port; ++p) {
        if (s->ports[p].live && s->ports[p].owner == t) retire_port(s, p);
        s->send[t][p] = s->once[t][p] = 0;
    }
    s->alive[t] = 0;
    pthread_cond_broadcast(&s->changed);
    rc = PHASE22E_OK;
end:
    pthread_mutex_unlock(&s->mu); return rc;
}
int phase22e_rights_get(phase22e_space *s, uint32_t t, uint32_t p, phase22e_rights *r) {
    if (!s || !r || !external(s, r, sizeof(*r))) return PHASE22E_BAD;
    pthread_mutex_lock(&s->mu);
    int rc = PHASE22E_RIGHT;
    if (valid_task(s, t) && p && p <= s->last_port) {
        *r = (phase22e_rights){s->send[t][p], s->once[t][p],
            s->ports[p].live && s->ports[p].owner == t, s->ports[p].retired};
        rc = PHASE22E_OK;
    }
    pthread_mutex_unlock(&s->mu); return rc;
}
int phase22e_stats_get(phase22e_space *s, phase22e_stats *out) {
    if (!s || !out || !external(s, out, sizeof(*out))) return PHASE22E_BAD;
    pthread_mutex_lock(&s->mu); *out = s->stats; pthread_mutex_unlock(&s->mu);
    return PHASE22E_OK;
}
int phase22e_port_diagnostics(phase22e_space *s, uint32_t t,
                              uint32_t p, phase22e_port_diag *out) {
    if (!s || !out || !external(s, out, sizeof(*out))) return PHASE22E_BAD;
    pthread_mutex_lock(&s->mu);
    int rc = PHASE22E_RIGHT;
    if (valid_task(s, t) && p && p <= s->last_port &&
        (s->ports[p].owner == t || s->send[t][p] || s->once[t][p])) {
        port_state *ps = &s->ports[p];
        phase22e_port_diag diag = {0};
        diag.owner = ps->owner;
        diag.live = ps->live;
        diag.queued = ps->used;
        diag.in_flight = ps->inflight;
        diag.service_registered = !!s->services[p].count;
        for (p22_waiter *w = ps->send_waiters; w; w = w->next)
            ++diag.send_waiters;
        for (p22_waiter *w = ps->recv_waiters; w; w = w->next)
            ++diag.receive_waiters;
        if (ps->used)
            diag.next_message_id = (int32_t)le32(ps->queue[ps->first].wire + 20);
        *out = diag;
        rc = PHASE22E_OK;
    }
    pthread_mutex_unlock(&s->mu);
    return rc;
}
int phase22e_cancel_waits(phase22e_space *s) {
    if (!s) return PHASE22E_BAD;
    pthread_mutex_lock(&s->mu);
    ++s->cancel_generation;
    pthread_cond_broadcast(&s->changed);
    pthread_mutex_unlock(&s->mu);
    return PHASE22E_OK;
}
/* Parse LE wire into caller-private, fixed-capacity metadata; never follow a
 * guest address. Mixed port and one OOL descriptor supported. */
typedef struct {
    uint32_t to, reply, ports[PHASE22E_DESCRIPTORS];
    uint8_t modes[PHASE22E_DESCRIPTORS], to_mode, reply_mode;
    unsigned count;
    size_t ool_offset, ool_size;
    uint64_t guest_offset;
} parsed;
static int parse(const uint8_t *b, size_t n, size_t arena_n, parsed *p) {
    if (n < 24 || n > PHASE22E_WIRE_MAX || (n & 3u)) return PHASE22E_SIZE;
    uint32_t bits = le32(b), declared = le32(b + 4);
    if (declared != n || le32(b + 16)) return PHASE22E_BAD;
    p->to = le32(b + 8); p->reply = le32(b + 12);
    p->to_mode = (uint8_t)(bits & 255u);
    p->reply_mode = (uint8_t)((bits >> 8) & 255u);
    if (!(p->to_mode == PHASE22E_COPY_SEND || p->to_mode == PHASE22E_MOVE_SEND ||
          p->to_mode == PHASE22E_MOVE_SEND_ONCE)) return PHASE22E_UNSUPPORTED;
    if (p->reply_mode != 0 && p->reply_mode != PHASE22E_MAKE_SEND_ONCE)
        return PHASE22E_UNSUPPORTED;
    if ((p->reply_mode == 0) != (p->reply == 0)) return PHASE22E_BAD;
    if (bits & ~(PHASE22E_COMPLEX | UINT32_C(0xffff))) return PHASE22E_UNSUPPORTED;
    if (!(bits & PHASE22E_COMPLEX)) return PHASE22E_OK;
    if (n < 28) return PHASE22E_SIZE;
    uint32_t count = le32(b + 24);
    if (!count || count > PHASE22E_DESCRIPTORS) return PHASE22E_UNSUPPORTED;
    size_t offset = 28;
    for (uint32_t i = 0; i < count; ++i) {
        if (offset > n || n - offset < 12) return PHASE22E_SIZE;
        uint8_t kind = b[offset + 11];
        if (kind == PHASE22E_DESC_PORT) {
            if (le32(b + offset + 4) || b[offset + 8] || b[offset + 9]) return PHASE22E_BAD;
            uint8_t mode = b[offset + 10];
            if (mode != PHASE22E_COPY_SEND && mode != PHASE22E_MOVE_SEND)
                return PHASE22E_UNSUPPORTED;
            p->ports[p->count] = le32(b + offset);
            p->modes[p->count++] = mode;
            offset += 12;
        } else if (kind == PHASE22E_DESC_OOL) {
            if (p->ool_size || n - offset < 16) return PHASE22E_UNSUPPORTED;
            if (b[offset + 8] || b[offset + 9] > 1 || b[offset + 10])
                return PHASE22E_UNSUPPORTED;
            p->guest_offset = le64(b + offset);
            p->ool_size = le32(b + offset + 12);
            if (!p->ool_size || p->ool_size > PHASE22E_OOL_MAX ||
                p->guest_offset > arena_n || p->ool_size > arena_n - (size_t)p->guest_offset)
                return PHASE22E_BAD;
            p->ool_offset = offset;
            offset += 16;
        } else return PHASE22E_UNSUPPORTED;
    }
    if (offset > n) return PHASE22E_SIZE;
    /* No unsized trailers: remaining bytes are opaque application payload. */
    return PHASE22E_OK;
}
static int can_send(phase22e_space *s, uint32_t t, uint32_t p, uint8_t mode) {
    return valid_port(s, p) && (mode == PHASE22E_MOVE_SEND_ONCE ?
        s->once[t][p] != 0 : s->send[t][p] != 0);
}
int phase22e_send(phase22e_space *s, uint32_t t, const void *wire, size_t n,
                  const uint8_t *arena, size_t arena_n, uint32_t timeout_ms) {
    if (!s || !wire || n > PHASE22E_WIRE_MAX || !external(s, wire, n) ||
        !external(s, arena, arena_n)) return preflight_fail(s, PHASE22E_BAD);
    /* Preflight + snapshot external bytes before taking the lock. */
    uint8_t copy[PHASE22E_WIRE_MAX];
    if (n < 24) return preflight_fail(s, PHASE22E_SIZE);
    memcpy(copy, wire, n);
    parsed p = {0};
    int rc = parse(copy, n, arena_n, &p);
    if (rc != PHASE22E_OK) return preflight_fail(s, rc);
    uint8_t ool[PHASE22E_OOL_MAX];
    if (p.ool_size) {
        if (!arena || overlap(wire, n, arena + (size_t)p.guest_offset, p.ool_size))
            return preflight_fail(s, PHASE22E_BAD);
        memcpy(ool, arena + (size_t)p.guest_offset, p.ool_size);
    }
    struct timespec until;
    if (timeout_ms) deadline(&until, timeout_ms);
    pthread_mutex_lock(&s->mu);
    uint64_t gen = s->cancel_generation;
    p22_waiter waiter = {0};
    port_state *wait_port = NULL;
    for (;;) {
        if (!valid_task(s, t)) { rc = PHASE22E_RIGHT; break; }
        if (!can_send(s, t, p.to, p.to_mode)) { rc = PHASE22E_RIGHT; break; }
        port_state *dest = &s->ports[p.to];
        uint32_t to = dest->owner;
        if (!valid_task(s, to)) { rc = PHASE22E_DEAD; break; }
        if (p.reply_mode && (!valid_port(s, p.reply) ||
            s->ports[p.reply].owner != t)) { rc = PHASE22E_RIGHT; break; }
        uint32_t moves[PHASE22E_PORTS + 1] = {0};
        uint32_t ones[PHASE22E_PORTS + 1] = {0};
        if (p.to_mode == PHASE22E_MOVE_SEND) ++moves[p.to];
        if (p.to_mode == PHASE22E_MOVE_SEND_ONCE) ++ones[p.to];
        for (unsigned i = 0; i < p.count; ++i) {
            uint32_t a = p.ports[i];
            if (!a || a > s->last_port || !can_send(s, t, a, p.modes[i])) break;
            if (p.modes[i] == PHASE22E_MOVE_SEND) ++moves[a];
        }
        rc = PHASE22E_OK;
        for (unsigned i = 0; i < p.count; ++i)
            if (!p.ports[i] || p.ports[i] > s->last_port ||
                !can_send(s, t, p.ports[i], p.modes[i])) { rc = PHASE22E_RIGHT; break; }
        if (rc) break;
        for (uint32_t a = 1; a <= s->last_port; ++a) {
            if (moves[a] > s->send[t][a] || ones[a] > s->once[t][a]) {
                rc = PHASE22E_RIGHT; break;
            }
        }
        if (rc) break;
        uint32_t increments[PHASE22E_PORTS + 1] = {0};
        for (unsigned i = 0; i < p.count; ++i) ++increments[p.ports[i]];
        for (uint32_t a = 1; a <= s->last_port; ++a)
            if (increments[a] && (increments[a] > P22_REFS - s->send[to][a] - s->reserved_send[to][a])) {
                rc = PHASE22E_QUOTA; break;
            }
        if (rc) break;
        if (p.reply_mode && s->once[to][p.reply] + s->reserved_once[to][p.reply] == P22_REFS) {
            rc = PHASE22E_QUOTA; break;
        }
        if (dest->used < PHASE22E_QUEUE &&
            s->stats.ool_bytes_pending + p.ool_size <= P22_OOL_BUDGET &&
            (!dest->send_waiters || dest->send_waiters == &waiter)) {
            message *m = &dest->queue[(dest->first + dest->used) % PHASE22E_QUEUE];
            memset(m, 0, sizeof(*m));
            memcpy(m->wire, copy, n);
            m->wire_size = n; m->ool_size = p.ool_size;
            if (p.ool_size) memcpy(m->ool, ool, p.ool_size);
            m->grant_count = p.count;
            for (unsigned i = 0; i < p.count; ++i) {
                m->grants[i] = p.ports[i];
                ++s->reserved_send[to][p.ports[i]];
                ++s->ports[p.ports[i]].inflight;
                ++s->stats.in_flight_transfers;
            }
            if (p.reply_mode) {
                m->reply = p.reply;
                m->reply_mode = p.reply_mode;
                ++s->reserved_once[to][p.reply];
                ++s->ports[p.reply].inflight;
                ++s->stats.in_flight_transfers;
            }
            for (uint32_t a = 1; a <= s->last_port; ++a) {
                s->send[t][a] -= moves[a]; s->once[t][a] -= ones[a];
            }
            ++dest->used; ++s->stats.sends; ++s->stats.messages_pending;
            s->stats.ool_bytes_pending += p.ool_size;
            pthread_cond_broadcast(&s->changed);
            rc = PHASE22E_OK; break;
        }
        if (!timeout_ms) { rc = PHASE22E_TIMEOUT; break; }
        if (!wait_port) {
            wait_port = dest;
            join_waiters(&dest->send_waiters, &waiter);
        }
        rc = wait_changed(s, gen, &until);
        if (rc) break;
    }
    if (wait_port) {
        leave_waiters(&wait_port->send_waiters, &waiter);
        pthread_cond_broadcast(&s->changed);
    }
    mark_rejected(s, rc);
    pthread_mutex_unlock(&s->mu);
    return rc;
}
int phase22e_receive(phase22e_space *s, uint32_t t, uint32_t p,
                     void *out, size_t *out_n, void *ool, size_t *ool_n,
                     uint32_t timeout_ms) {
    if (!s || !out || !out_n || !ool_n || !external(s, out_n, sizeof(*out_n)) ||
        !external(s, ool_n, sizeof(*ool_n))) return preflight_fail(s, PHASE22E_BAD);
    size_t cap = *out_n, oolcap = *ool_n;
    if (!external(s, out, cap) || !external(s, ool, oolcap) ||
        overlap(out, cap, out_n, sizeof(*out_n)) ||
        overlap(out, cap, ool_n, sizeof(*ool_n)) ||
        overlap(ool, oolcap, out_n, sizeof(*out_n)) ||
        overlap(ool, oolcap, ool_n, sizeof(*ool_n)) ||
        overlap(out, cap, ool, oolcap) || overlap(out_n, sizeof(*out_n), ool_n, sizeof(*ool_n)))
        return preflight_fail(s, PHASE22E_BAD);
    struct timespec until;
    if (timeout_ms) deadline(&until, timeout_ms);
    pthread_mutex_lock(&s->mu);
    uint64_t gen = s->cancel_generation;
    p22_waiter waiter = {0};
    port_state *wait_port = NULL;
    int rc;
    for (;;) {
        if (!valid_task(s, t) || !valid_port(s, p) || s->ports[p].owner != t) {
            rc = PHASE22E_RIGHT; break;
        }
        port_state *ps = &s->ports[p];
        if (ps->used && (!ps->recv_waiters || ps->recv_waiters == &waiter)) {
            message *m = &ps->queue[ps->first];
            if (cap < m->wire_size || oolcap < m->ool_size) { rc = PHASE22E_SIZE; break; }
            uint8_t tmp[PHASE22E_WIRE_MAX];
            memcpy(tmp, m->wire, m->wire_size);
            /* The destination is rewritten to the actual receive name.
             * Local reply right, if present, becomes a send-once destination. */
            wr32(tmp + 8, p);
            if (m->reply_mode) {
                wr32(tmp + 12, 0);
                wr32(tmp + 8, m->reply); /* receiver's reply endpoint */
                uint32_t bits = le32(tmp);
                bits = (bits & ~UINT32_C(0xffff)) | PHASE22E_MOVE_SEND_ONCE;
                wr32(tmp, bits);
            }
            for (unsigned i = 0; i < m->grant_count; ++i) {
                uint32_t a = m->grants[i];
                --s->reserved_send[t][a];
                --s->ports[a].inflight; --s->stats.in_flight_transfers;
                if (s->ports[a].live) ++s->send[t][a];
            }
            if (m->reply_mode) {
                --s->reserved_once[t][m->reply];
                --s->ports[m->reply].inflight; --s->stats.in_flight_transfers;
                if (s->ports[m->reply].live) ++s->once[t][m->reply];
            }
            /* Raw sender-side guest addresses must not escape to recipients.
             * The payload is returned separately through the bounded OOL out. */
            if (m->ool_size) {
                size_t off = 28;
                uint32_t count = le32(tmp + 24);
                for (uint32_t i = 0; i < count; ++i) {
                    if (tmp[off + 11] == PHASE22E_DESC_OOL) {
                        memset(tmp + off, 0, 8);
                        off += 16;
                    } else off += 12;
                }
            }
            memcpy(out, tmp, m->wire_size);
            if (m->ool_size) memcpy(ool, m->ool, m->ool_size);
            *out_n = m->wire_size; *ool_n = m->ool_size;
            s->stats.ool_bytes_pending -= m->ool_size;
            --s->stats.messages_pending;
            memset(m, 0, sizeof(*m));
            ps->first = (ps->first + 1) % PHASE22E_QUEUE;
            --ps->used; ++s->stats.receives;
            pthread_cond_broadcast(&s->changed);
            rc = PHASE22E_OK; break;
        }
        if (!timeout_ms) { rc = PHASE22E_TIMEOUT; break; }
        if (!wait_port) {
            wait_port = ps;
            join_waiters(&ps->recv_waiters, &waiter);
        }
        rc = wait_changed(s, gen, &until);
        if (rc) break;
    }
    if (wait_port) {
        leave_waiters(&wait_port->recv_waiters, &waiter);
        pthread_cond_broadcast(&s->changed);
    }
    mark_rejected(s, rc);
    pthread_mutex_unlock(&s->mu);
    return rc;
}
int phase22e_mig_dispatch(const phase22e_method *methods, size_t count,
                          const void *req, size_t n, void *out, size_t *out_n) {
    if (!methods || !req || !out || !out_n || n < 24 || n > PHASE22E_WIRE_MAX ||
        *out_n < 24 || count > 128 || overlap(req, n, out, *out_n) ||
        overlap(out, *out_n, out_n, sizeof(*out_n))) return PHASE22E_BAD;
    const uint8_t *bytes = req;
    uint32_t bits = le32(bytes);
    if (le32(bytes + 4) != n || (bits & PHASE22E_COMPLEX) ||
        ((bits >> 16) & 0x7fff) || le32(bytes + 16)) return PHASE22E_BAD;
    int32_t id = (int32_t)le32(bytes + 20);
    for (size_t i = 0; i < count; ++i) {
        const phase22e_method *m = &methods[i];
        if (m->request_id != id) continue;
        if (!m->invoke || n != m->request_size || *out_n < m->response_size ||
            m->response_size > PHASE22E_WIRE_MAX || m->response_size < 24)
            return PHASE22E_SIZE;
        uint8_t reply[PHASE22E_WIRE_MAX] = {0};
        size_t actual = m->response_size;
        int rc = m->invoke(m->ctx, bytes, n, reply, &actual);
        if (rc) return rc;
        if (actual != m->response_size) return PHASE22E_SIZE;
        /* Handler emits payload only; broker stamps and validates envelope. */
        memcpy(reply, bytes, 24);
        wr32(reply, PHASE22E_MOVE_SEND_ONCE);
        wr32(reply + 4, (uint32_t)actual);
        wr32(reply + 8, le32(bytes + 8));
        wr32(reply + 12, 0); wr32(reply + 16, 0);
        wr32(reply + 20, (uint32_t)id + UINT32_C(100));
        memcpy(out, reply, actual);
        *out_n = actual;
        return PHASE22E_OK;
    }
    return PHASE22E_UNSUPPORTED;
}

int phase22e_service_register(phase22e_space *s, uint32_t t, uint32_t p,
                              const phase22e_method *methods, size_t count) {
    if (!s || !methods || !count || count > PHASE22E_METHODS ||
        !external(s, methods, count * sizeof(*methods))) return PHASE22E_BAD;
    phase22e_method copy[PHASE22E_METHODS];
    memcpy(copy, methods, count * sizeof(*methods));
    for (size_t i = 0; i < count; ++i) {
        if (!copy[i].invoke || copy[i].request_size < 24 ||
            copy[i].request_size > PHASE22E_WIRE_MAX ||
            copy[i].response_size < 24 ||
            copy[i].response_size > PHASE22E_WIRE_MAX) return PHASE22E_BAD;
        for (size_t j = 0; j < i; ++j)
            if (copy[i].request_id == copy[j].request_id) return PHASE22E_BAD;
    }
    pthread_mutex_lock(&s->mu);
    int rc = PHASE22E_RIGHT;
    if (valid_task(s, t) && valid_port(s, p) && s->ports[p].owner == t) {
        service *v = &s->services[p];
        if (v->count || v->active) rc = PHASE22E_QUOTA;
        else {
            memcpy(v->methods, copy, count * sizeof(*copy));
            v->count = (unsigned)count; v->owner = t;
            rc = PHASE22E_OK;
        }
    }
    pthread_mutex_unlock(&s->mu);
    return rc;
}
int phase22e_service_unregister(phase22e_space *s, uint32_t t, uint32_t p) {
    if (!s) return PHASE22E_BAD;
    pthread_mutex_lock(&s->mu);
    int rc = PHASE22E_RIGHT;
    if (valid_task(s, t) && valid_port(s, p) && s->ports[p].owner == t &&
        s->services[p].count) {
        if (s->services[p].active) rc = PHASE22E_QUOTA;
        else {
            memset(&s->services[p], 0, sizeof(s->services[p]));
            rc = PHASE22E_OK;
        }
    }
    pthread_mutex_unlock(&s->mu);
    return rc;
}
int phase22e_service_dispatch(phase22e_space *s, uint32_t t, uint32_t p,
                              const void *request, size_t request_length,
                              void *reply, size_t *reply_length) {
    if (!s || !request || !reply || !reply_length ||
        request_length > PHASE22E_WIRE_MAX ||
        !external(s, request, request_length) ||
        !external(s, reply_length, sizeof(*reply_length)) ||
        !external(s, reply, *reply_length)) return PHASE22E_BAD;
    phase22e_method methods[PHASE22E_METHODS];
    unsigned count = 0;
    pthread_mutex_lock(&s->mu);
    if (valid_task(s, t) && valid_port(s, p) &&
        s->ports[p].owner == t && s->services[p].count) {
        service *v = &s->services[p];
        count = v->count;
        memcpy(methods, v->methods, count * sizeof(*methods));
        ++v->active;
    }
    pthread_mutex_unlock(&s->mu);
    if (!count) return PHASE22E_RIGHT;
    int rc = phase22e_mig_dispatch(methods, count, request, request_length,
                                   reply, reply_length);
    pthread_mutex_lock(&s->mu);
    --s->services[p].active;
    pthread_cond_broadcast(&s->changed);
    pthread_mutex_unlock(&s->mu);
    return rc;
}
