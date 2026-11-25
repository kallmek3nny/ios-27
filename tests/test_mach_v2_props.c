#include "ios27/mach_v2.h"

#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * HANDOFF phase-15 item 4: deterministic, bounded property tests for the
 * synthetic task-local Mach rights fixture declared in ios27/mach_v2.h.
 *
 * Fixed xorshift32 seeds drive operation sequences against a small
 * reference model. After every step the model is compared with
 * ios27_mach_v2_rights() for every name ever issued:
 *   1. Stale names: a retired receive name or send alias never becomes
 *      valid again, and no API reissues a name the task has already seen.
 *   2. In-flight transfers: a queued cross-task port descriptor blocks the
 *      owner's destroy and both owner and receiver teardown, atomically,
 *      until the receiver dequeues it.
 *   3. Queue saturation: a full queue rejects plain, MOVE_SEND and complex
 *      sends with TIMEOUT, debits no right and preserves FIFO order.
 * A failure names the property, seed and step so the run can be replayed.
 * This exercises the in-process fixture only; it is not XNU behaviour.
 */

static const char *g_property = "setup";
static uint32_t g_seed;
static uint32_t g_state;
static unsigned g_step;
static struct {
    unsigned long steps, saturated, stale_probes, blocked, received, transfers;
} g_stats;

static void fail(int line, const char *expression) {
    fprintf(stderr,
            "test_mach_v2_props.c:%d: REQUIRE(%s) failed "
            "[property=%s seed=0x%08" PRIx32 " step=%u]\n",
            line, expression, g_property, g_seed, g_step);
    abort();
}
#define REQUIRE(condition) do { if (!(condition)) fail(__LINE__, #condition); } while (0)

static uint32_t next_random(void) {
    uint32_t x = g_state;
    x ^= x << 13u;
    x ^= x >> 17u;
    x ^= x << 5u;
    g_state = x;
    return x;
}
static uint32_t below(uint32_t bound) {
    return next_random() % bound;
}
static void reseed(const char *property, uint32_t seed) {
    g_property = property;
    g_seed = seed;
    g_state = seed ^ UINT32_C(0x9e3779b9);
    if (!g_state) g_state = 1u;
    g_step = 0u;
    for (unsigned i = 0u; i < 16u; ++i) (void)next_random();
}

static void wr32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0u; i < 4u; ++i) p[i] = (uint8_t)(value >> (8u * i));
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
/* 24-byte mach_msg_header_t with no reply, voucher or body. */
static size_t plain(uint8_t p[IOS27_MACH_MSG_MAX], uint32_t remote,
                    uint32_t disposition, uint32_t id) {
    memset(p, 0, IOS27_MACH_MSG_MAX);
    wr32(p, disposition);
    wr32(p + 4u, 24u);
    wr32(p + 8u, remote);
    wr32(p + 20u, id);
    return 24u;
}
/* COPY_SEND destination plus `count` port descriptors naming `item`;
 * bit i of move_mask selects MOVE_SEND instead of COPY_SEND. */
static size_t complex_message(uint8_t p[IOS27_MACH_MSG_MAX], uint32_t remote,
                              uint32_t id, uint32_t item, unsigned count,
                              unsigned move_mask) {
    size_t size = 28u + 12u * (size_t)count;
    memset(p, 0, IOS27_MACH_MSG_MAX);
    wr32(p, IOS27_MACH_COMPLEX | IOS27_MACH_COPY_SEND);
    wr32(p + 4u, (uint32_t)size);
    wr32(p + 8u, remote);
    wr32(p + 20u, id);
    wr32(p + 24u, count);
    for (unsigned i = 0u; i < count; ++i) {
        size_t off = 28u + 12u * (size_t)i;
        wr32(p + off, item);
        p[off + 10u] = (uint8_t)(((move_mask >> i) & 1u) ?
                                 IOS27_MACH_MOVE_SEND : IOS27_MACH_COPY_SEND);
        p[off + 11u] = IOS27_MACH_PORT_DESCRIPTOR;
    }
    return size;
}
/* sends == 0 && receive == 0 means the name must be dead (BAD_RIGHT). */
static void expect_rights(const ios27_mach_v2 *s, uint32_t task,
                          uint32_t name, uint32_t sends, unsigned receive) {
    uint32_t a = UINT32_MAX, b = UINT32_MAX;
    unsigned c = UINT_MAX;
    int rc = ios27_mach_v2_rights(s, task, name, &a, &b, &c);
    if (!sends && !receive) {
        REQUIRE(rc == IOS27_MACH_BAD_RIGHT);
        return;
    }
    REQUIRE(rc == IOS27_MACH_OK);
    REQUIRE(a == sends && b == 0u && c == receive);
}
static void receive_plain(ios27_mach_v2 *s, uint32_t task, uint32_t name,
                          uint32_t id) {
    uint8_t out[IOS27_MACH_MSG_MAX];
    size_t cap = sizeof(out);
    REQUIRE(ios27_mach_v2_receive(s, task, name, out, &cap, 0u) ==
            IOS27_MACH_OK);
    REQUIRE(cap == 24u && rd32(out + 4u) == 24u);
    REQUIRE(rd32(out + 8u) == 0u && rd32(out + 12u) == name);
    REQUIRE(rd32(out + 20u) == id);
    ++g_stats.received;
}

/* ---- Properties 1 and 3: random operation soup over tasks 1..4. ---- */
#define SOUP_TASKS 4u
#define SOUP_STEPS 400u
#define SOUP_RECORDS 512u /* each step issues at most one name */

typedef struct {
    int live;
    uint32_t owner, name;
    uint32_t queue[IOS27_MACH_QUEUE_MAX];
    unsigned head, count;
} model_port;

typedef struct {
    int live;
    uint32_t task, name, urefs;
    unsigned port;
} model_alias;

typedef struct {
    ios27_mach_v2 *s;
    model_port ports[SOUP_RECORDS];
    model_alias aliases[SOUP_RECORDS];
    uint32_t seen[SOUP_TASKS + 1u][SOUP_RECORDS];
    unsigned nports, naliases, nseen[SOUP_TASKS + 1u];
    uint32_t next_id;
} soup;

static soup g_soup;

/* Property 1: every issued name is non-null and never recycled. */
static void soup_issue(soup *m, uint32_t task, uint32_t name) {
    REQUIRE(name != 0u);
    for (unsigned i = 0u; i < m->nseen[task]; ++i)
        REQUIRE(m->seen[task][i] != name);
    REQUIRE(m->nseen[task] < SOUP_RECORDS);
    m->seen[task][m->nseen[task]++] = name;
}
static unsigned soup_live_ports(const soup *m) {
    unsigned n = 0u;
    for (unsigned i = 0u; i < m->nports; ++i) n += m->ports[i].live ? 1u : 0u;
    return n;
}
static int soup_pick_port(const soup *m) {
    unsigned live[SOUP_RECORDS], n = 0u;
    for (unsigned i = 0u; i < m->nports; ++i)
        if (m->ports[i].live) live[n++] = i;
    return n ? (int)live[below(n)] : -1;
}
static int soup_pick_alias(const soup *m) {
    unsigned live[SOUP_RECORDS], n = 0u;
    for (unsigned i = 0u; i < m->naliases; ++i)
        if (m->aliases[i].live) live[n++] = i;
    return n ? (int)live[below(n)] : -1;
}
static model_alias *soup_alias_for(soup *m, uint32_t task, unsigned port) {
    for (unsigned i = 0u; i < m->naliases; ++i)
        if (m->aliases[i].live && m->aliases[i].task == task &&
            m->aliases[i].port == port)
            return &m->aliases[i];
    return NULL;
}
static void soup_kill_port(soup *m, unsigned index) {
    m->ports[index].live = 0;
    m->ports[index].count = 0u;
    for (unsigned i = 0u; i < m->naliases; ++i)
        if (m->aliases[i].port == index) m->aliases[i].live = 0;
}
/* Each record owns exactly one never-recycled name: live records must
 * report exact rights and retired records must stay BAD_RIGHT forever. */
static void soup_audit(const soup *m) {
    for (unsigned i = 0u; i < m->nports; ++i) {
        const model_port *p = &m->ports[i];
        expect_rights(m->s, p->owner, p->name, 0u, p->live ? 1u : 0u);
    }
    for (unsigned i = 0u; i < m->naliases; ++i) {
        const model_alias *a = &m->aliases[i];
        expect_rights(m->s, a->task, a->name, a->live ? a->urefs : 0u, 0u);
    }
}
static void soup_allocate(soup *m, uint32_t task) {
    uint32_t name = 0u;
    int rc = ios27_mach_v2_allocate(m->s, task, &name);
    model_port *p;
    if (soup_live_ports(m) >= IOS27_MACH_PORT_MAX) {
        REQUIRE(rc == IOS27_MACH_LIMIT);
        return;
    }
    REQUIRE(rc == IOS27_MACH_OK);
    soup_issue(m, task, name);
    REQUIRE(m->nports < SOUP_RECORDS);
    p = &m->ports[m->nports++];
    memset(p, 0, sizeof(*p));
    p->live = 1;
    p->owner = task;
    p->name = name;
}
static void soup_make_send(soup *m) {
    int index = soup_pick_port(m);
    uint32_t recipient, name = 0u;
    model_port *p;
    model_alias *a;
    int rc;
    if (index < 0) {
        soup_allocate(m, 1u + below(SOUP_TASKS));
        return;
    }
    p = &m->ports[index];
    do recipient = 1u + below(SOUP_TASKS); while (recipient == p->owner);
    a = soup_alias_for(m, recipient, (unsigned)index);
    rc = ios27_mach_v2_make_send(m->s, p->owner, p->name, recipient, &name);
    if (a && a->urefs >= IOS27_MACH_RIGHT_MAX) {
        REQUIRE(rc == IOS27_MACH_LIMIT);
        return;
    }
    REQUIRE(rc == IOS27_MACH_OK);
    if (a) {
        REQUIRE(name == a->name); /* one alias per port per task */
        ++a->urefs;
        return;
    }
    soup_issue(m, recipient, name);
    REQUIRE(m->naliases < SOUP_RECORDS);
    a = &m->aliases[m->naliases++];
    a->live = 1;
    a->task = recipient;
    a->name = name;
    a->urefs = 1u;
    a->port = (unsigned)index;
}
static void soup_send(soup *m, int move) {
    int index = soup_pick_alias(m);
    uint8_t wire[IOS27_MACH_MSG_MAX];
    model_alias *a;
    model_port *p;
    uint32_t id;
    int rc;
    if (index < 0) {
        soup_make_send(m);
        return;
    }
    a = &m->aliases[index];
    p = &m->ports[a->port];
    id = ++m->next_id;
    (void)plain(wire, a->name,
                move ? IOS27_MACH_MOVE_SEND : IOS27_MACH_COPY_SEND, id);
    rc = ios27_mach_v2_send(m->s, a->task, wire, 24u, 0u);
    if (p->count == IOS27_MACH_QUEUE_MAX) {
        /* Property 3: saturation never debits MOVE_SEND or reorders. */
        REQUIRE(rc == IOS27_MACH_TIMEOUT);
        ++g_stats.saturated;
        return;
    }
    REQUIRE(rc == IOS27_MACH_OK);
    p->queue[(p->head + p->count) % IOS27_MACH_QUEUE_MAX] = id;
    ++p->count;
    if (move && --a->urefs == 0u) a->live = 0;
}
static void soup_receive(soup *m) {
    int index = soup_pick_port(m);
    uint8_t out[IOS27_MACH_MSG_MAX];
    size_t cap = sizeof(out);
    model_port *p;
    int rc;
    if (index < 0) {
        soup_allocate(m, 1u + below(SOUP_TASKS));
        return;
    }
    p = &m->ports[index];
    if (p->count && below(4u) == 0u) {
        memset(out, 0x5a, sizeof(out));
        cap = 23u;
        REQUIRE(ios27_mach_v2_receive(m->s, p->owner, p->name, out, &cap,
                                      0u) == IOS27_MACH_TOO_LARGE);
        REQUIRE(cap == 24u && out[0] == 0x5au);
        return;
    }
    rc = ios27_mach_v2_receive(m->s, p->owner, p->name, out, &cap, 0u);
    if (!p->count) {
        REQUIRE(rc == IOS27_MACH_TIMEOUT);
        return;
    }
    REQUIRE(rc == IOS27_MACH_OK);
    REQUIRE(cap == 24u && rd32(out + 4u) == 24u);
    REQUIRE(rd32(out + 8u) == 0u && rd32(out + 12u) == p->name);
    REQUIRE(rd32(out + 20u) == p->queue[p->head]);
    p->head = (p->head + 1u) % IOS27_MACH_QUEUE_MAX;
    --p->count;
    ++g_stats.received;
}
static void soup_drop(soup *m) {
    int index = soup_pick_alias(m);
    model_alias *a;
    if (index < 0) return;
    a = &m->aliases[index];
    REQUIRE(ios27_mach_v2_drop_send(m->s, a->task, a->name) == IOS27_MACH_OK);
    if (--a->urefs == 0u) a->live = 0;
}
static void soup_destroy(soup *m) {
    int index = soup_pick_port(m);
    model_port *p;
    if (index < 0) return;
    p = &m->ports[index];
    REQUIRE(ios27_mach_v2_destroy(m->s, p->owner, p->name) == IOS27_MACH_OK);
    soup_kill_port(m, (unsigned)index);
}
static void soup_teardown(soup *m, uint32_t task) {
    REQUIRE(ios27_mach_v2_task_teardown(m->s, task) == IOS27_MACH_OK);
    for (unsigned i = 0u; i < m->nports; ++i)
        if (m->ports[i].live && m->ports[i].owner == task)
            soup_kill_port(m, i);
    for (unsigned i = 0u; i < m->naliases; ++i)
        if (m->aliases[i].task == task) m->aliases[i].live = 0;
}
/* Property 1: every API rejects a retired name in its own task. */
static void soup_probe_stale(soup *m) {
    uint32_t task = 1u + below(SOUP_TASKS), dead[2u * SOUP_RECORDS];
    uint32_t name, other, out_name = 0u;
    uint8_t wire[IOS27_MACH_MSG_MAX];
    size_t cap = sizeof(wire);
    unsigned n = 0u;
    for (unsigned i = 0u; i < m->nports; ++i)
        if (!m->ports[i].live && m->ports[i].owner == task)
            dead[n++] = m->ports[i].name;
    for (unsigned i = 0u; i < m->naliases; ++i)
        if (!m->aliases[i].live && m->aliases[i].task == task)
            dead[n++] = m->aliases[i].name;
    if (!n) return;
    name = dead[below(n)];
    ++g_stats.stale_probes;
    switch (below(5u)) {
    case 0u:
        do other = 1u + below(SOUP_TASKS); while (other == task);
        REQUIRE(ios27_mach_v2_make_send(m->s, task, name, other,
                                        &out_name) == IOS27_MACH_BAD_RIGHT);
        break;
    case 1u:
        REQUIRE(ios27_mach_v2_drop_send(m->s, task, name) ==
                IOS27_MACH_BAD_RIGHT);
        break;
    case 2u:
        REQUIRE(ios27_mach_v2_destroy(m->s, task, name) ==
                IOS27_MACH_BAD_RIGHT);
        break;
    case 3u:
        REQUIRE(ios27_mach_v2_receive(m->s, task, name, wire, &cap, 0u) ==
                IOS27_MACH_BAD_RIGHT);
        break;
    default:
        (void)plain(wire, name, below(2u) ? IOS27_MACH_MOVE_SEND :
                                            IOS27_MACH_COPY_SEND,
                    ++m->next_id);
        REQUIRE(ios27_mach_v2_send(m->s, task, wire, 24u, 0u) ==
                IOS27_MACH_BAD_RIGHT);
        break;
    }
}
/* Wrong-role names fail closed; the following audit proves no effect. */
static void soup_probe_role(soup *m) {
    uint8_t wire[IOS27_MACH_MSG_MAX];
    size_t cap = sizeof(wire);
    uint32_t other, out_name = 0u;
    int port = soup_pick_port(m), alias = soup_pick_alias(m);
    const model_alias *a = alias < 0 ? NULL : &m->aliases[alias];
    REQUIRE(ios27_mach_v2_task_teardown(m->s, IOS27_MACH_TASK_MAX + 1u) ==
            IOS27_MACH_BAD_ARGUMENT);
    switch (below(4u)) {
    case 0u: /* a receive right alone grants no send right */
        if (port < 0) return;
        (void)plain(wire, m->ports[port].name, IOS27_MACH_COPY_SEND,
                    ++m->next_id);
        REQUIRE(ios27_mach_v2_send(m->s, m->ports[port].owner, wire, 24u,
                                   0u) == IOS27_MACH_BAD_RIGHT);
        break;
    case 1u:
        if (!a) return;
        REQUIRE(ios27_mach_v2_destroy(m->s, a->task, a->name) ==
                IOS27_MACH_BAD_RIGHT);
        break;
    case 2u:
        if (!a) return;
        do other = 1u + below(SOUP_TASKS); while (other == a->task);
        REQUIRE(ios27_mach_v2_make_send(m->s, a->task, a->name, other,
                                        &out_name) != IOS27_MACH_OK);
        break;
    default:
        if (!a) return;
        REQUIRE(ios27_mach_v2_receive(m->s, a->task, a->name, wire, &cap,
                                      0u) != IOS27_MACH_OK);
        break;
    }
}
static void soup_run(uint32_t seed) {
    soup *m = &g_soup;
    memset(m, 0, sizeof(*m));
    reseed("stale-names-and-saturation", seed);
    m->s = ios27_mach_v2_new();
    REQUIRE(m->s != NULL);
    for (g_step = 0u; g_step < SOUP_STEPS; ++g_step) {
        uint32_t roll = below(100u);
        if (roll < 12u) soup_allocate(m, 1u + below(SOUP_TASKS));
        else if (roll < 28u) soup_make_send(m);
        else if (roll < 46u) soup_send(m, 0);
        else if (roll < 54u) soup_send(m, 1);
        else if (roll < 70u) soup_receive(m);
        else if (roll < 78u) soup_drop(m);
        else if (roll < 84u) soup_destroy(m);
        else if (roll < 87u) soup_teardown(m, 1u + below(SOUP_TASKS));
        else if (roll < 95u) soup_probe_stale(m);
        else soup_probe_role(m);
        soup_audit(m);
        ++g_stats.steps;
    }
    /* Retire every task: all names ever issued must now be dead, and the
     * restarted tasks must receive only fresh names. */
    for (uint32_t t = 1u; t <= SOUP_TASKS; ++t) soup_teardown(m, t);
    REQUIRE(soup_live_ports(m) == 0u);
    soup_audit(m);
    for (uint32_t t = 1u; t <= SOUP_TASKS; ++t) soup_allocate(m, t);
    soup_audit(m);
    ios27_mach_v2_free(m->s);
}

/* ---- Properties 2 and 3: cross-task descriptor transfer scenarios. ---- */
#define FLIGHT_SCENARIOS 128u
#define FLIGHT_RETIRED 4u

typedef struct {
    ios27_mach_v2 *s;
    uint32_t own, snd, rcv;
    uint32_t port, alias, inbox, inbox_alias, rcv_alias;
    uint32_t alias_urefs, rcv_urefs;
    int own_live, rcv_live;
    uint32_t retired_task[FLIGHT_RETIRED], retired_name[FLIGHT_RETIRED];
    unsigned retired;
} flight;

static void flight_verify(const flight *f) {
    expect_rights(f->s, f->own, f->port, 0u, f->own_live ? 1u : 0u);
    expect_rights(f->s, f->snd, f->alias,
                  f->own_live ? f->alias_urefs : 0u, 0u);
    if (f->rcv_alias)
        expect_rights(f->s, f->rcv, f->rcv_alias,
                      f->own_live && f->rcv_live ? f->rcv_urefs : 0u, 0u);
    expect_rights(f->s, f->rcv, f->inbox, 0u, f->rcv_live ? 1u : 0u);
    expect_rights(f->s, f->snd, f->inbox_alias, f->rcv_live ? 1u : 0u, 0u);
    for (unsigned i = 0u; i < f->retired; ++i)
        expect_rights(f->s, f->retired_task[i], f->retired_name[i], 0u, 0u);
}
static void flight_plain(const flight *f, uint32_t id, int expected) {
    uint8_t wire[IOS27_MACH_MSG_MAX];
    (void)plain(wire, f->inbox_alias, IOS27_MACH_COPY_SEND, id);
    REQUIRE(ios27_mach_v2_send(f->s, f->snd, wire, 24u, 0u) == expected);
}
static void flight_run(uint32_t seed) {
    flight f;
    uint8_t wire[IOS27_MACH_MSG_MAX], out[IOS27_MACH_MSG_MAX];
    uint32_t name = 0u, received, fresh = 0u;
    unsigned count, moves = 0u, move_mask = 0u, fill, head = 0u, queued;
    unsigned pre, rounds, extra = 0u;
    size_t size, cap;
    int rc;

    memset(&f, 0, sizeof(f));
    reseed("in-flight-transfers", seed);
    f.s = ios27_mach_v2_new();
    REQUIRE(f.s != NULL);
    f.own = 1u + below(IOS27_MACH_TASK_MAX);
    do f.snd = 1u + below(IOS27_MACH_TASK_MAX); while (f.snd == f.own);
    do f.rcv = 1u + below(IOS27_MACH_TASK_MAX);
    while (f.rcv == f.own || f.rcv == f.snd);
    f.own_live = f.rcv_live = 1;
    REQUIRE(ios27_mach_v2_allocate(f.s, f.own, &f.port) == IOS27_MACH_OK);
    REQUIRE(ios27_mach_v2_allocate(f.s, f.rcv, &f.inbox) == IOS27_MACH_OK);
    f.alias_urefs = 1u + below(4u);
    for (unsigned i = 0u; i < f.alias_urefs; ++i) {
        REQUIRE(ios27_mach_v2_make_send(f.s, f.own, f.port, f.snd, &name) ==
                IOS27_MACH_OK);
        REQUIRE(name != 0u && (i == 0u || name == f.alias));
        f.alias = name;
    }
    REQUIRE(ios27_mach_v2_make_send(f.s, f.rcv, f.inbox, f.snd,
                                    &f.inbox_alias) == IOS27_MACH_OK);
    REQUIRE(f.inbox_alias != 0u && f.inbox_alias != f.alias);
    pre = below(3u) == 0u ? 1u + below(2u) : 0u;
    for (unsigned i = 0u; i < pre; ++i) {
        REQUIRE(ios27_mach_v2_make_send(f.s, f.own, f.port, f.rcv, &name) ==
                IOS27_MACH_OK);
        REQUIRE(name != 0u && name != f.inbox &&
                (i == 0u || name == f.rcv_alias));
        f.rcv_alias = name;
    }
    f.rcv_urefs = pre;
    flight_verify(&f);

    g_step = 1u;
    count = 1u + below(IOS27_MACH_V2_DESCRIPTORS);
    for (unsigned i = 0u; i < count; ++i)
        if (below(2u)) {
            move_mask |= 1u << i;
            ++moves;
        }
    fill = below(IOS27_MACH_QUEUE_MAX + 1u);
    for (unsigned i = 0u; i < fill; ++i)
        flight_plain(&f, 1000u + i, IOS27_MACH_OK);
    queued = fill;
    size = complex_message(wire, f.inbox_alias, 7000u, f.alias, count,
                           move_mask);
    /* Too few urefs: rejected without any debit or enqueue. */
    if (moves > f.alias_urefs && queued < IOS27_MACH_QUEUE_MAX) {
        REQUIRE(ios27_mach_v2_send(f.s, f.snd, wire, size, 0u) ==
                IOS27_MACH_BAD_RIGHT);
        flight_verify(&f);
    }
    while (f.alias_urefs < moves) {
        REQUIRE(ios27_mach_v2_make_send(f.s, f.own, f.port, f.snd, &name) ==
                IOS27_MACH_OK);
        REQUIRE(name == f.alias);
        ++f.alias_urefs;
    }
    flight_verify(&f);

    g_step = 2u;
    if (queued == IOS27_MACH_QUEUE_MAX) {
        /* Property 3: a full queue rejects the transfer, debiting nothing. */
        REQUIRE(ios27_mach_v2_send(f.s, f.snd, wire, size, 0u) ==
                IOS27_MACH_TIMEOUT);
        ++g_stats.saturated;
        flight_verify(&f);
        receive_plain(f.s, f.rcv, f.inbox, 1000u + head++);
        --queued;
    }
    REQUIRE(ios27_mach_v2_send(f.s, f.snd, wire, size, 0u) == IOS27_MACH_OK);
    ++queued;
    ++g_stats.transfers;
    f.alias_urefs -= moves;
    flight_verify(&f);
    if (!f.alias_urefs)
        REQUIRE(ios27_mach_v2_drop_send(f.s, f.snd, f.alias) ==
                IOS27_MACH_BAD_RIGHT);

    g_step = 3u;
    /* Property 2: queued foreign credits block owner destroy and both
     * owner and receiver teardown, repeatedly and without side effects. */
    rounds = 1u + below(3u);
    for (unsigned r = 0u; r < rounds; ++r) {
        unsigned first = below(3u);
        for (unsigned j = 0u; j < 3u; ++j) {
            switch ((first + j) % 3u) {
            case 0u: rc = ios27_mach_v2_task_teardown(f.s, f.own); break;
            case 1u: rc = ios27_mach_v2_destroy(f.s, f.own, f.port); break;
            default: rc = ios27_mach_v2_task_teardown(f.s, f.rcv); break;
            }
            REQUIRE(rc == IOS27_MACH_BAD_RIGHT);
            ++g_stats.blocked;
            flight_verify(&f);
        }
    }

    g_step = 4u;
    if (below(2u)) {
        int expected = queued < IOS27_MACH_QUEUE_MAX ? IOS27_MACH_OK :
                                                       IOS27_MACH_TIMEOUT;
        flight_plain(&f, 9000u, expected);
        if (expected == IOS27_MACH_OK) {
            extra = 1u;
            ++queued;
        } else {
            ++g_stats.saturated;
        }
        flight_verify(&f);
    }

    g_step = 5u;
    while (head < fill) receive_plain(f.s, f.rcv, f.inbox, 1000u + head++);
    if (below(2u)) {
        memset(out, 0x79, sizeof(out));
        cap = size - 1u;
        REQUIRE(ios27_mach_v2_receive(f.s, f.rcv, f.inbox, out, &cap, 0u) ==
                IOS27_MACH_TOO_LARGE);
        REQUIRE(cap == size && out[0] == 0x79u);
        REQUIRE(ios27_mach_v2_task_teardown(f.s, f.own) ==
                IOS27_MACH_BAD_RIGHT);
        ++g_stats.blocked;
        flight_verify(&f);
    }
    cap = sizeof(out);
    REQUIRE(ios27_mach_v2_receive(f.s, f.rcv, f.inbox, out, &cap, 0u) ==
            IOS27_MACH_OK);
    REQUIRE(cap == size && rd32(out + 4u) == (uint32_t)size);
    REQUIRE(rd32(out) == IOS27_MACH_COMPLEX);
    REQUIRE(rd32(out + 8u) == 0u && rd32(out + 12u) == f.inbox);
    REQUIRE(rd32(out + 20u) == 7000u && rd32(out + 24u) == count);
    received = rd32(out + 28u);
    for (unsigned i = 0u; i < count; ++i) {
        size_t off = 28u + 12u * (size_t)i;
        REQUIRE(rd32(out + off) == received);
        REQUIRE(out[off + 10u] == IOS27_MACH_MOVE_SEND);
        REQUIRE(out[off + 11u] == IOS27_MACH_PORT_DESCRIPTOR);
    }
    /* Delivered rights merge into an existing alias, else a fresh name. */
    if (pre) REQUIRE(received == f.rcv_alias);
    else REQUIRE(received != 0u && received != f.inbox);
    f.rcv_alias = received;
    f.rcv_urefs = pre + count;
    ++g_stats.received;
    flight_verify(&f);
    if (extra) receive_plain(f.s, f.rcv, f.inbox, 9000u);
    cap = sizeof(out);
    REQUIRE(ios27_mach_v2_receive(f.s, f.rcv, f.inbox, out, &cap, 0u) ==
            IOS27_MACH_TIMEOUT);

    g_step = 6u;
    if (!f.alias_urefs) {
        /* Property 1: a fully moved alias is not recycled for its port. */
        REQUIRE(ios27_mach_v2_make_send(f.s, f.own, f.port, f.snd, &name) ==
                IOS27_MACH_OK);
        REQUIRE(name != 0u && name != f.alias && name != f.inbox_alias);
        REQUIRE(f.retired < FLIGHT_RETIRED);
        f.retired_task[f.retired] = f.snd;
        f.retired_name[f.retired++] = f.alias;
        f.alias = name;
        f.alias_urefs = 1u;
        flight_verify(&f);
    }

    g_step = 7u;
    if (below(2u)) {
        REQUIRE(ios27_mach_v2_task_teardown(f.s, f.own) == IOS27_MACH_OK);
        f.own_live = 0;
        flight_verify(&f);
        REQUIRE(ios27_mach_v2_task_teardown(f.s, f.rcv) == IOS27_MACH_OK);
        f.rcv_live = 0;
    } else {
        REQUIRE(ios27_mach_v2_task_teardown(f.s, f.rcv) == IOS27_MACH_OK);
        f.rcv_live = 0;
        flight_verify(&f);
        REQUIRE(ios27_mach_v2_task_teardown(f.s, f.own) == IOS27_MACH_OK);
        f.own_live = 0;
    }
    flight_verify(&f);
    REQUIRE(ios27_mach_v2_task_teardown(f.s, f.snd) == IOS27_MACH_OK);

    g_step = 8u;
    /* Retired tasks restart with fresh names only. */
    REQUIRE(ios27_mach_v2_allocate(f.s, f.own, &fresh) == IOS27_MACH_OK);
    REQUIRE(fresh != 0u && fresh != f.port);
    REQUIRE(ios27_mach_v2_allocate(f.s, f.rcv, &fresh) == IOS27_MACH_OK);
    REQUIRE(fresh != 0u && fresh != f.inbox && fresh != f.rcv_alias);
    REQUIRE(ios27_mach_v2_allocate(f.s, f.snd, &fresh) == IOS27_MACH_OK);
    REQUIRE(fresh != 0u && fresh != f.alias && fresh != f.inbox_alias);
    for (unsigned i = 0u; i < f.retired; ++i)
        REQUIRE(fresh != f.retired_name[i]);
    ios27_mach_v2_free(f.s);
}

int main(void) {
    static const uint32_t soup_seeds[] = {
        UINT32_C(0x1d4b09ee), UINT32_C(0x9e3779b9), UINT32_C(0x85ebca6b),
        UINT32_C(0xc2b2ae35), UINT32_C(0x27d4eb2f), UINT32_C(0x165667b1),
        UINT32_C(0xd3a2646c), UINT32_C(0xfd7046c5)};
    const size_t seeds = sizeof(soup_seeds) / sizeof(soup_seeds[0]);
    for (size_t i = 0u; i < seeds; ++i) soup_run(soup_seeds[i]);
    for (uint32_t i = 0u; i < FLIGHT_SCENARIOS; ++i)
        flight_run(UINT32_C(0x5eed0000) + i);
    reseed("coverage", 0u);
    REQUIRE(g_stats.saturated > 0u && g_stats.stale_probes > 0u);
    REQUIRE(g_stats.blocked >= 3u * FLIGHT_SCENARIOS);
    REQUIRE(g_stats.transfers == FLIGHT_SCENARIOS);
    printf("mach rights properties: %zu soup seeds x %u steps (%lu total), "
           "%u transfer scenarios; %lu saturated sends rejected, %lu stale "
           "probes, %lu blocked retirements, %lu messages received; PASS\n",
           seeds, SOUP_STEPS, g_stats.steps, FLIGHT_SCENARIOS,
           g_stats.saturated, g_stats.stale_probes, g_stats.blocked,
           g_stats.received);
    return 0;
}
