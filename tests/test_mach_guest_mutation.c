#include "ios27/mach_guest.h"

#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Y t-ce4d0f: deterministic mutation tests for the bounded guest-memory
 * Mach boundary in src/mach_guest.c (8263d7a), driven host-side with no
 * native exec. tests/test_mach_guest.c and tests/test_mach_v2_props.c
 * (F t-d4f570) already cover well-formed wires, the SEND and RECEIVE caps,
 * host-result aliasing and queue saturation. This file adds the gaps from
 * the crew baseline note:
 *   - one byte over and exactly at the RECEIVE_SET, RECEIVE_TRAILER and
 *     RECEIVE_OOL caps;
 *   - msgh_size disagreeing with the call length in both directions;
 *   - descriptor counts of 0, the fixture limit, one over it, UINT32_MAX
 *     and counts whose 12-byte descriptors wrap 32-bit size arithmetic;
 *   - OOL address+length one past the arena end, truncated to 32 bits or
 *     wrapping 2^64;
 *   - odd (unaligned) guest offsets for SEND, RECEIVE and RECEIVE_OOL;
 *   - fixed-seed every-prefix truncation and bit-flip sweeps.
 *
 * Every SEND image is copied into an exactly sized malloc arena, so an
 * over-read past the wire or the OOL payload is an ASan error. A rejected
 * call must fail closed against a twin fixture built by the same calls:
 * the arena and *actual are unchanged, ios27_mach_v2_rights() agrees for
 * every scanned (task, name), every receive queue is still empty and each
 * task allocates the same next name (no port-namespace change). Where the
 * fixture contract leaves acceptance open, an accepted message must still
 * be self-consistent. A failure names the case, seed and step.
 * This exercises the in-process fixture only; it is not XNU behaviour.
 */

#define SENTINEL UINT32_C(0xdeadbeef)
#define PLAIN_WIRE 24u
#define OOL_WIRE 56u
#define OOL_PAYLOAD 48u
#define OOL_IMAGE (OOL_WIRE + OOL_PAYLOAD)
#define PORTS_MAX ((IOS27_MACH_MSG_MAX - 28u) / 12u)
#define SCAN_TASKS 4u
#define SCAN_NAMES (2u * IOS27_MACH_V2_NAMES)
#define FLIP_STEPS 256u

enum { EITHER = 0, MUST_REJECT = 1, MUST_ACCEPT = 2 };

static const char *g_case = "setup";
static uint32_t g_seed;
static uint32_t g_state;
static unsigned g_step;
static struct {
    unsigned long attempts, rejected, accepted;
} g_stats;

static void fail(int line, const char *expression) {
    fprintf(stderr,
            "test_mach_guest_mutation.c:%d: REQUIRE(%s) failed "
            "[case=%s seed=0x%08" PRIx32 " step=%u]\n",
            line, expression, g_case, g_seed, g_step);
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
static void begin(const char *name, uint32_t seed) {
    g_case = name;
    g_seed = seed;
    g_state = seed ^ UINT32_C(0x9e3779b9);
    if (!g_state) g_state = 1u;
    g_step = 0u;
    for (unsigned i = 0u; i < 16u; ++i) (void)next_random();
}

static void wr32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0u; i < 4u; ++i) p[i] = (uint8_t)(value >> (8u * i));
}
static void wr64(uint8_t *p, uint64_t value) {
    for (unsigned i = 0u; i < 8u; ++i) p[i] = (uint8_t)(value >> (8u * i));
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static void flip(uint8_t *p, uint32_t bit) {
    p[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
}

typedef struct {
    ios27_mach_v2 *s;
    uint32_t srv;   /* task 1 receive right: every message's destination */
    uint32_t item;  /* task 3 receive right carried by port descriptors */
    uint32_t cli;   /* task 2 receive right for MAKE_SEND_ONCE replies */
    uint32_t dest;  /* task 2 send alias for srv */
    uint32_t alias; /* task 2 send alias for item (one uref) */
} fixture;

static fixture g_names;
static int g_names_ready;

static void fx_open(fixture *f) {
    f->s = ios27_mach_v2_new();
    REQUIRE(f->s != NULL);
    REQUIRE(ios27_mach_v2_allocate(f->s, 1u, &f->srv) == IOS27_MACH_OK);
    REQUIRE(ios27_mach_v2_allocate(f->s, 3u, &f->item) == IOS27_MACH_OK);
    REQUIRE(ios27_mach_v2_allocate(f->s, 2u, &f->cli) == IOS27_MACH_OK);
    REQUIRE(ios27_mach_v2_make_send(f->s, 1u, f->srv, 2u, &f->dest) ==
            IOS27_MACH_OK);
    REQUIRE(ios27_mach_v2_make_send(f->s, 3u, f->item, 2u, &f->alias) ==
            IOS27_MACH_OK);
    /* Wire images are built once, so every fixture must reissue the same
     * names for the same call sequence. */
    if (g_names_ready)
        REQUIRE(f->srv == g_names.srv && f->item == g_names.item &&
                f->cli == g_names.cli && f->dest == g_names.dest &&
                f->alias == g_names.alias);
}
static void fx_close(fixture *f) {
    ios27_mach_v2_free(f->s);
    f->s = NULL;
}

static void expect_same_rights(const fixture *m, const fixture *t,
                               uint32_t task, uint32_t name) {
    uint32_t ms = SENTINEL, mo = SENTINEL, ts = SENTINEL, to = SENTINEL;
    unsigned mr = UINT_MAX, tr = UINT_MAX;
    int mrc = ios27_mach_v2_rights(m->s, task, name, &ms, &mo, &mr);
    int trc = ios27_mach_v2_rights(t->s, task, name, &ts, &to, &tr);
    REQUIRE(mrc == trc);
    if (mrc == IOS27_MACH_OK) REQUIRE(ms == ts && mo == to && mr == tr);
}
static void expect_empty(fixture *f, uint32_t task, uint32_t name) {
    uint8_t out[IOS27_MACH_MSG_MAX];
    size_t cap = sizeof(out);
    REQUIRE(ios27_mach_v2_receive(f->s, task, name, out, &cap, 0u) ==
            IOS27_MACH_TIMEOUT);
}
/* Fail closed: a rejected call leaves the mutant indistinguishable from
 * its untouched twin, including the name each task allocates next. */
static void expect_twin(fixture *m, fixture *t) {
    const uint32_t known[5] = { m->srv, m->item, m->cli, m->dest, m->alias };
    for (uint32_t task = 1u; task <= SCAN_TASKS; ++task) {
        for (uint32_t name = 0u; name <= SCAN_NAMES; ++name)
            expect_same_rights(m, t, task, name);
        for (unsigned i = 0u; i < 5u; ++i)
            expect_same_rights(m, t, task, known[i]);
    }
    expect_empty(m, 1u, m->srv);
    expect_empty(m, 2u, m->cli);
    expect_empty(m, 3u, m->item);
    expect_empty(t, 1u, t->srv);
    expect_empty(t, 2u, t->cli);
    expect_empty(t, 3u, t->item);
    for (uint32_t task = 1u; task <= 3u; ++task) {
        uint32_t mn = SENTINEL, tn = SENTINEL;
        int mrc = ios27_mach_v2_allocate(m->s, task, &mn);
        int trc = ios27_mach_v2_allocate(t->s, task, &tn);
        REQUIRE(mrc == trc);
        if (mrc == IOS27_MACH_OK) REQUIRE(mn == tn);
    }
}

static int known_error(int rc) {
    return rc == IOS27_MACH_BAD_ARGUMENT || rc == IOS27_MACH_BAD_RIGHT ||
           rc == IOS27_MACH_LIMIT || rc == IOS27_MACH_TIMEOUT ||
           rc == IOS27_MACH_TOO_LARGE || rc == IOS27_MACH_UNSUPPORTED;
}

typedef void (*accept_check)(fixture *m, uint64_t length);

/* An accepted plain message is delivered with a self-consistent size. */
static void plain_consistent(fixture *m, uint64_t length) {
    uint8_t out[IOS27_MACH_MSG_MAX];
    uint32_t actual = SENTINEL;
    REQUIRE(ios27_mach_guest_call(m->s, 1u, IOS27_MACH_GUEST_RECEIVE, m->srv,
                                  out, sizeof(out), 0u, sizeof(out), 0u,
                                  &actual) == IOS27_MACH_OK);
    REQUIRE(actual >= PLAIN_WIRE && actual <= length);
    REQUIRE(rd32(out + 4u) == actual);
}
/* A descriptor-shaped tail without MACH_MSGH_BITS_COMPLEX is inline data:
 * it may be accepted, but must never move the encoded right. */
static void inline_tail(fixture *m, uint64_t length) {
    uint32_t sends = 0u, once = 0u;
    unsigned receive = 0u;
    REQUIRE(ios27_mach_v2_rights(m->s, 2u, m->alias, &sends, &once,
                                 &receive) == IOS27_MACH_OK);
    REQUIRE(sends == 1u);
    plain_consistent(m, length);
}

/* SEND `length` bytes at `offset` from an exactly sized heap copy of
 * `image`. A reject must fail closed; an accept runs the optional check. */
static int attempt_send(const uint8_t *image, size_t arena_size,
                        uint64_t offset, uint64_t length, int expect,
                        accept_check check) {
    fixture m, t;
    uint8_t *arena, *snapshot;
    uint32_t actual = SENTINEL;
    int rc;
    REQUIRE(arena_size > 0u);
    arena = malloc(arena_size);
    snapshot = malloc(arena_size);
    REQUIRE(arena != NULL && snapshot != NULL);
    memcpy(arena, image, arena_size);
    memcpy(snapshot, image, arena_size);
    fx_open(&m);
    fx_open(&t);
    ++g_stats.attempts;
    rc = ios27_mach_guest_call(m.s, 2u, IOS27_MACH_GUEST_SEND, 0u, arena,
                               arena_size, offset, length, 0u, &actual);
    /* SEND only reads guest memory, whether or not it is accepted. */
    REQUIRE(memcmp(arena, snapshot, arena_size) == 0);
    if (rc == IOS27_MACH_OK) {
        if (expect == MUST_REJECT)
            fprintf(stderr, "mutant accepted: length %" PRIu64 "\n", length);
        REQUIRE(expect != MUST_REJECT);
        REQUIRE(actual == (uint32_t)length);
        if (check) check(&m, length);
        ++g_stats.accepted;
    } else {
        if (expect == MUST_ACCEPT)
            fprintf(stderr, "well-formed wire rejected: rc %d\n", rc);
        REQUIRE(expect != MUST_ACCEPT);
        REQUIRE(known_error(rc));
        REQUIRE(actual == SENTINEL);
        expect_twin(&m, &t);
        ++g_stats.rejected;
    }
    fx_close(&m);
    fx_close(&t);
    free(arena);
    free(snapshot);
    return rc;
}

/* 24-byte COPY_SEND to srv, optionally with a MAKE_SEND_ONCE reply. */
static void build_plain(uint8_t *w, uint32_t id, int reply) {
    memset(w, 0, PLAIN_WIRE);
    wr32(w, IOS27_MACH_COPY_SEND |
                (reply ? IOS27_MACH_MAKE_SEND_ONCE << 8u : 0u));
    wr32(w + 4u, PLAIN_WIRE);
    wr32(w + 8u, g_names.dest);
    wr32(w + 12u, reply ? g_names.cli : 0u);
    wr32(w + 20u, id);
}
/* Complex wire: a MOVE_SEND port descriptor for item, then one LP64 OOL
 * descriptor whose payload fills the image bytes right after the wire. */
static void build_ool(uint8_t *a, uint32_t id, uint8_t fill) {
    memset(a, 0, OOL_IMAGE);
    wr32(a, IOS27_MACH_COMPLEX | IOS27_MACH_COPY_SEND);
    wr32(a + 4u, OOL_WIRE);
    wr32(a + 8u, g_names.dest);
    wr32(a + 20u, id);
    wr32(a + 24u, 2u);
    wr32(a + 28u, g_names.alias);
    a[38] = IOS27_MACH_MOVE_SEND;
    a[39] = IOS27_MACH_PORT_DESCRIPTOR;
    wr64(a + 40u, OOL_WIRE);
    a[49] = 1u; /* MACH_MSG_VIRTUAL_COPY */
    a[51] = IOS27_MACH_OOL_DESCRIPTOR;
    wr32(a + 52u, OOL_PAYLOAD);
    for (size_t i = 0u; i < OOL_PAYLOAD; ++i)
        a[OOL_WIRE + i] = (uint8_t)(fill + i);
}
/* Complex wire of `size` bytes whose body claims `count` descriptors;
 * only the first `present` port descriptors for item are encoded. */
static void build_ports(uint8_t *w, uint32_t size, uint32_t count,
                        unsigned present, uint8_t disposition) {
    REQUIRE(present <= PORTS_MAX);
    memset(w, 0, IOS27_MACH_MSG_MAX);
    wr32(w, IOS27_MACH_COMPLEX | IOS27_MACH_COPY_SEND);
    wr32(w + 4u, size);
    wr32(w + 8u, g_names.dest);
    wr32(w + 20u, 0x5900u + present);
    wr32(w + 24u, count);
    for (unsigned i = 0u; i < present; ++i) {
        size_t off = 28u + 12u * (size_t)i;
        wr32(w + off, g_names.alias);
        w[off + 10u] = disposition;
        w[off + 11u] = IOS27_MACH_PORT_DESCRIPTOR;
    }
}

static void bases_accepted(void) {
    uint8_t w[OOL_IMAGE];
    begin("well-formed-bases", 0u);
    build_plain(w, 0x5101u, 1);
    (void)attempt_send(w, PLAIN_WIRE, 0u, PLAIN_WIRE, MUST_ACCEPT,
                       plain_consistent);
    g_step = 1u;
    build_ool(w, 0x5102u, 0x41u);
    (void)attempt_send(w, OOL_IMAGE, 0u, OOL_WIRE, MUST_ACCEPT, NULL);
}

/* Per-operation caps from src/mach_guest.c: one byte over fails before any
 * dequeue with the arena and result untouched; exactly at the cap the
 * still-queued message is delivered. */
static void receive_caps(void) {
    static const struct {
        uint32_t operation;
        uint64_t length;
    } over[] = {
        {IOS27_MACH_GUEST_RECEIVE_SET, IOS27_MACH_MSG_MAX + 1u},
        {IOS27_MACH_GUEST_RECEIVE_TRAILER,
         IOS27_MACH_MSG_MAX + IOS27_MACH_SEQNO_TRAILER_SIZE + 1u},
        {IOS27_MACH_GUEST_RECEIVE_OOL,
         IOS27_MACH_MSG_MAX + IOS27_MACH_OOL_MAX + 1u},
    };
    fixture f;
    uint8_t arena[1024], before[1024], img[OOL_IMAGE];
    uint32_t actual, set = 0u, delivered, sends = 0u, once = 0u;
    unsigned receive = 0u;
    begin("receive-caps", 0u);
    fx_open(&f);
    REQUIRE(ios27_mach_v2_set_allocate(f.s, 1u, &set) == IOS27_MACH_OK);
    REQUIRE(ios27_mach_v2_set_add(f.s, 1u, set, f.srv) == IOS27_MACH_OK);
    memset(arena, 0x3c, sizeof(arena));
    build_plain(img, 0x5c01u, 0);
    memcpy(arena, img, PLAIN_WIRE);
    actual = SENTINEL;
    REQUIRE(ios27_mach_guest_call(f.s, 2u, IOS27_MACH_GUEST_SEND, 0u, arena,
                                  sizeof(arena), 0u, PLAIN_WIRE, 0u,
                                  &actual) == IOS27_MACH_OK &&
            actual == PLAIN_WIRE);
    for (size_t i = 0u; i < sizeof(over) / sizeof(over[0]); ++i) {
        uint32_t name = over[i].operation == IOS27_MACH_GUEST_RECEIVE_SET ?
                        set : f.srv;
        g_step = (unsigned)i;
        memcpy(before, arena, sizeof(arena));
        actual = SENTINEL;
        REQUIRE(ios27_mach_guest_call(f.s, 1u, over[i].operation, name,
                                      arena, sizeof(arena), 512u,
                                      over[i].length, 0u, &actual) ==
                IOS27_MACH_BAD_ARGUMENT);
        REQUIRE(actual == SENTINEL);
        REQUIRE(memcmp(arena, before, sizeof(arena)) == 0);
    }
    g_step = 10u;
    REQUIRE(ios27_mach_guest_call(f.s, 1u, IOS27_MACH_GUEST_RECEIVE_TRAILER,
                                  f.srv, arena, sizeof(arena), 512u,
                                  IOS27_MACH_MSG_MAX +
                                      IOS27_MACH_SEQNO_TRAILER_SIZE,
                                  0u, &actual) == IOS27_MACH_OK);
    REQUIRE(actual == PLAIN_WIRE + IOS27_MACH_SEQNO_TRAILER_SIZE);
    REQUIRE(rd32(arena + 512u + 4u) == PLAIN_WIRE);
    REQUIRE(rd32(arena + 512u + 20u) == 0x5c01u);
    g_step = 11u;
    build_plain(img, 0x5c02u, 0);
    memcpy(arena, img, PLAIN_WIRE);
    REQUIRE(ios27_mach_guest_call(f.s, 2u, IOS27_MACH_GUEST_SEND, 0u, arena,
                                  sizeof(arena), 0u, PLAIN_WIRE, 0u,
                                  &actual) == IOS27_MACH_OK);
    REQUIRE(ios27_mach_guest_call(f.s, 1u, IOS27_MACH_GUEST_RECEIVE_SET, set,
                                  arena, sizeof(arena), 512u,
                                  IOS27_MACH_MSG_MAX, 0u, &actual) ==
            IOS27_MACH_OK && actual == PLAIN_WIRE);
    REQUIRE(rd32(arena + 512u + 20u) == 0x5c02u);
    g_step = 12u;
    build_ool(img, 0x5c03u, 0x61u);
    memcpy(arena, img, OOL_IMAGE);
    REQUIRE(ios27_mach_guest_call(f.s, 2u, IOS27_MACH_GUEST_SEND, 0u, arena,
                                  sizeof(arena), 0u, OOL_WIRE, 0u,
                                  &actual) == IOS27_MACH_OK &&
            actual == OOL_WIRE);
    memcpy(before, arena, sizeof(arena));
    actual = SENTINEL;
    REQUIRE(ios27_mach_guest_call(f.s, 1u, IOS27_MACH_GUEST_RECEIVE_OOL, f.srv,
                                  arena, sizeof(arena), 600u,
                                  IOS27_MACH_MSG_MAX + IOS27_MACH_OOL_MAX + 1u,
                                  0u, &actual) == IOS27_MACH_BAD_ARGUMENT);
    REQUIRE(actual == SENTINEL);
    REQUIRE(memcmp(arena, before, sizeof(arena)) == 0);
    REQUIRE(ios27_mach_guest_call(f.s, 1u, IOS27_MACH_GUEST_RECEIVE_OOL, f.srv,
                                  arena, sizeof(arena), 600u,
                                  IOS27_MACH_MSG_MAX + IOS27_MACH_OOL_MAX, 0u,
                                  &actual) == IOS27_MACH_OK &&
            actual == OOL_IMAGE);
    REQUIRE(rd32(arena + 600u + 4u) == OOL_WIRE);
    REQUIRE(rd32(arena + 600u + 20u) == 0x5c03u);
    REQUIRE(rd32(arena + 600u + 40u) == 600u + OOL_WIRE);
    REQUIRE(rd32(arena + 600u + 44u) == 0u);
    REQUIRE(memcmp(arena + 600u + OOL_WIRE, img + OOL_WIRE, OOL_PAYLOAD) == 0);
    delivered = rd32(arena + 600u + 28u);
    REQUIRE(ios27_mach_v2_rights(f.s, 1u, delivered, &sends, &once,
                                 &receive) == IOS27_MACH_OK && sends == 1u);
    fx_close(&f);
}

/* msgh_size disagreeing with the call length, both ways, plus unaligned
 * and cap-sized plain wires: reject fail-closed or deliver consistently. */
static void size_mismatch(void) {
    static const struct {
        uint32_t msgh_size, length;
    } cases[] = {
        {28u, 24u}, {24u, 28u}, {20u, 24u}, {0u, 24u}, {UINT32_MAX, 24u},
        {IOS27_MACH_MSG_MAX + 1u, 24u}, {26u, 26u}, {25u, 28u},
        {IOS27_MACH_MSG_MAX, IOS27_MACH_MSG_MAX - 4u},
        {IOS27_MACH_MSG_MAX, IOS27_MACH_MSG_MAX},
    };
    uint8_t w[IOS27_MACH_MSG_MAX];
    begin("msgh-size-vs-length", 0u);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        g_step = (unsigned)i;
        memset(w, 0, sizeof(w));
        build_plain(w, 0x5300u + (uint32_t)i, 1);
        wr32(w + 4u, cases[i].msgh_size);
        (void)attempt_send(w, cases[i].length, 0u, cases[i].length, EITHER,
                           plain_consistent);
    }
}

static void descriptor_counts(void) {
    static const uint32_t overflowing[] = {
        2u, 3u, IOS27_MACH_V2_DESCRIPTORS, IOS27_MACH_V2_DESCRIPTORS + 1u,
        UINT32_C(0x15555556), /* 12 * count wraps to 8 in 32 bits */
        UINT32_C(0x2aaaaaab), /* 12 * count wraps to 4 in 32 bits */
        UINT32_C(0x80000000), UINT32_MAX,
    };
    const uint32_t limit = IOS27_MACH_V2_DESCRIPTORS;
    uint8_t w[IOS27_MACH_MSG_MAX];
    begin("descriptor-counts", 0u);
    /* The fixture limit itself is accepted; one more is rejected. */
    build_ports(w, 28u + 12u * limit, limit, limit, IOS27_MACH_COPY_SEND);
    (void)attempt_send(w, 28u + 12u * limit, 0u, 28u + 12u * limit,
                       MUST_ACCEPT, NULL);
    g_step = 1u;
    build_ports(w, 28u + 12u * (limit + 1u), limit + 1u, limit + 1u,
                IOS27_MACH_COPY_SEND);
    (void)attempt_send(w, 28u + 12u * (limit + 1u), 0u,
                       28u + 12u * (limit + 1u), MUST_REJECT, NULL);
    /* One encoded MOVE_SEND descriptor; the count claims more. */
    for (size_t i = 0u; i < sizeof(overflowing) / sizeof(overflowing[0]); ++i) {
        g_step = 2u + (unsigned)i;
        build_ports(w, 40u, overflowing[i], 1u, IOS27_MACH_MOVE_SEND);
        (void)attempt_send(w, 40u, 0u, 40u, MUST_REJECT, NULL);
    }
    /* A count of one with no descriptor bytes at all. */
    g_step = 100u;
    build_ports(w, 28u, 1u, 0u, IOS27_MACH_MOVE_SEND);
    (void)attempt_send(w, 28u, 0u, 28u, MUST_REJECT, NULL);
    /* An empty complex body may be accepted, otherwise it fails closed. */
    g_step = 101u;
    build_ports(w, 28u, 0u, 0u, IOS27_MACH_MOVE_SEND);
    (void)attempt_send(w, 28u, 0u, 28u, EITHER, NULL);
    /* Without the complex bit the same bytes are inline data. */
    g_step = 102u;
    build_ports(w, 40u, 1u, 1u, IOS27_MACH_MOVE_SEND);
    wr32(w, IOS27_MACH_COPY_SEND);
    (void)attempt_send(w, 40u, 0u, 40u, EITHER, inline_tail);
}

static void ool_span_edges(void) {
    static const struct {
        uint64_t address;
        uint32_t size;
        int expect;
    } cases[] = {
        {OOL_WIRE, OOL_PAYLOAD, MUST_ACCEPT},      /* ends at the arena end */
        {OOL_WIRE + 1u, OOL_PAYLOAD, MUST_REJECT}, /* one byte past it */
        {OOL_WIRE, OOL_PAYLOAD + 1u, MUST_REJECT}, /* length one past it */
        {OOL_IMAGE, OOL_PAYLOAD, MUST_REJECT},     /* starts at the end */
        {OOL_IMAGE, 1u, MUST_REJECT},
        {UINT64_C(0x100000000) + OOL_WIRE, OOL_PAYLOAD, MUST_REJECT},
        {UINT64_C(0x8000000000000000) + OOL_WIRE, OOL_PAYLOAD, MUST_REJECT},
        {UINT64_MAX - OOL_PAYLOAD + 1u, OOL_PAYLOAD, MUST_REJECT}, /* 2^64 */
        {UINT64_MAX - 7u, OOL_PAYLOAD, MUST_REJECT}, /* end wraps to 40 */
        {UINT64_MAX, 1u, MUST_REJECT},               /* end wraps to 0 */
        {OOL_WIRE, UINT32_MAX, MUST_REJECT},
        {OOL_WIRE, 0u, EITHER},                      /* empty payload */
    };
    uint8_t img[OOL_IMAGE];
    begin("ool-span-edges", 0u);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        g_step = (unsigned)i;
        build_ool(img, 0x5f00u + (uint32_t)i, 0x41u);
        wr64(img + 40u, cases[i].address);
        wr32(img + 52u, cases[i].size);
        (void)attempt_send(img, OOL_IMAGE, 0u, OOL_WIRE, cases[i].expect,
                           NULL);
    }
}

/* Odd guest offsets in exactly sized heap arenas: the boundary stages
 * bytes privately, so no unaligned host access may reach the fixture. */
static void unaligned_offsets(void) {
    static const size_t odd[] = {1u, 3u, 5u, 7u, 33u, 61u};
    uint8_t img[OOL_IMAGE];
    begin("unaligned-offsets", 0u);
    for (size_t i = 0u; i < sizeof(odd) / sizeof(odd[0]); ++i) {
        const size_t off = odd[i], rx_off = odd[i] + 2u;
        const uint32_t plain_id = 0x5d00u + (uint32_t)i;
        const uint32_t ool_id = 0x5e00u + (uint32_t)i;
        uint8_t *tx = malloc(off + PLAIN_WIRE);
        uint8_t *rx = malloc(rx_off + PLAIN_WIRE);
        uint8_t *tx_ool = malloc(off + OOL_IMAGE);
        uint8_t *rx_ool = malloc(rx_off + OOL_IMAGE);
        uint32_t actual = SENTINEL, reply, delivered, sends = 0u, once = 0u;
        unsigned receive = 0u;
        fixture f;
        g_step = (unsigned)i;
        REQUIRE(tx && rx && tx_ool && rx_ool);
        fx_open(&f);
        memset(tx, 0xa5, off + PLAIN_WIRE);
        memset(rx, 0x5a, rx_off + PLAIN_WIRE);
        build_plain(img, plain_id, 1);
        memcpy(tx + off, img, PLAIN_WIRE);
        REQUIRE(ios27_mach_guest_call(f.s, 2u, IOS27_MACH_GUEST_SEND, 0u, tx,
                                      off + PLAIN_WIRE, off, PLAIN_WIRE, 0u,
                                      &actual) == IOS27_MACH_OK &&
                actual == PLAIN_WIRE);
        actual = SENTINEL;
        REQUIRE(ios27_mach_guest_call(f.s, 1u, IOS27_MACH_GUEST_RECEIVE, f.srv,
                                      rx, rx_off + PLAIN_WIRE, rx_off,
                                      PLAIN_WIRE, 0u, &actual) ==
                IOS27_MACH_OK && actual == PLAIN_WIRE);
        REQUIRE(rd32(rx + rx_off + 4u) == PLAIN_WIRE);
        REQUIRE(rd32(rx + rx_off + 12u) == f.srv);
        REQUIRE(rd32(rx + rx_off + 20u) == plain_id);
        reply = rd32(rx + rx_off + 8u);
        REQUIRE(reply != 0u && reply != f.srv);
        memset(tx_ool, 0xa5, off + OOL_IMAGE);
        memset(rx_ool, 0x5a, rx_off + OOL_IMAGE);
        build_ool(img, ool_id, (uint8_t)(0x30u + i));
        wr64(img + 40u, off + OOL_WIRE);
        memcpy(tx_ool + off, img, OOL_IMAGE);
        actual = SENTINEL;
        REQUIRE(ios27_mach_guest_call(f.s, 2u, IOS27_MACH_GUEST_SEND, 0u,
                                      tx_ool, off + OOL_IMAGE, off, OOL_WIRE,
                                      0u, &actual) == IOS27_MACH_OK &&
                actual == OOL_WIRE);
        actual = SENTINEL;
        REQUIRE(ios27_mach_guest_call(f.s, 1u, IOS27_MACH_GUEST_RECEIVE_OOL,
                                      f.srv, rx_ool, rx_off + OOL_IMAGE,
                                      rx_off, OOL_IMAGE, 0u, &actual) ==
                IOS27_MACH_OK && actual == OOL_IMAGE);
        REQUIRE(rd32(rx_ool + rx_off + 4u) == OOL_WIRE);
        REQUIRE(rd32(rx_ool + rx_off + 20u) == ool_id);
        REQUIRE(rd32(rx_ool + rx_off + 40u) == rx_off + OOL_WIRE);
        REQUIRE(rd32(rx_ool + rx_off + 44u) == 0u);
        REQUIRE(memcmp(rx_ool + rx_off + OOL_WIRE, img + OOL_WIRE,
                       OOL_PAYLOAD) == 0);
        delivered = rd32(rx_ool + rx_off + 28u);
        REQUIRE(ios27_mach_v2_rights(f.s, 1u, delivered, &sends, &once,
                                     &receive) == IOS27_MACH_OK &&
                sends == 1u);
        fx_close(&f);
        free(tx);
        free(rx);
        free(tx_ool);
        free(rx_ool);
    }
}

static void truncation_sweep(uint32_t seed) {
    uint8_t img[OOL_IMAGE], cut[16u + OOL_IMAGE];
    uint32_t id;
    uint8_t fill;
    begin("truncation", seed);
    id = next_random();
    fill = (uint8_t)next_random();
    build_plain(img, id, 1);
    /* Every prefix shorter than mach_msg_header_t fails in the boundary. */
    for (size_t len = 1u; len < PLAIN_WIRE; ++len) {
        g_step = (unsigned)len;
        REQUIRE(attempt_send(img, len, 0u, len, MUST_REJECT, NULL) ==
                IOS27_MACH_BAD_ARGUMENT);
    }
    build_ool(img, id, fill);
    for (size_t len = 1u; len < OOL_WIRE; ++len) {
        size_t odd = 1u + 2u * (size_t)below(8u);
        g_step = 100u + (unsigned)len;
        /* Original msgh_size: the header overstates the call length. */
        (void)attempt_send(img, len, 0u, len, MUST_REJECT, NULL);
        /* The same prefix at a seeded odd offset of an exact arena. */
        memset(cut, (int)fill, sizeof(cut));
        memcpy(cut + odd, img, len);
        (void)attempt_send(cut, odd + len, odd, len, MUST_REJECT, NULL);
        /* A self-consistent msgh_size still cuts a descriptor short. */
        if (len >= PLAIN_WIRE) {
            memcpy(cut, img, len);
            wr32(cut + 4u, (uint32_t)len);
            (void)attempt_send(cut, len, 0u, len, MUST_REJECT, NULL);
        }
    }
    /* The whole wire with its OOL payload cut short by the arena end. */
    for (size_t keep = 0u; keep < OOL_PAYLOAD; ++keep) {
        g_step = 200u + (unsigned)keep;
        (void)attempt_send(img, OOL_WIRE + keep, 0u, OOL_WIRE, MUST_REJECT,
                           NULL);
    }
}

static void bitflip_sweep(uint32_t seed) {
    uint8_t plain[PLAIN_WIRE], ool[OOL_IMAGE], mut[OOL_IMAGE];
    begin("bitflip", seed);
    build_plain(plain, next_random(), 1);
    build_ool(ool, next_random(), (uint8_t)next_random());
    /* Every single-bit flip of each wire either sends or fails closed. */
    for (uint32_t bit = 0u; bit < PLAIN_WIRE * 8u; ++bit) {
        g_step = bit;
        memcpy(mut, plain, PLAIN_WIRE);
        flip(mut, bit);
        (void)attempt_send(mut, PLAIN_WIRE, 0u, PLAIN_WIRE, EITHER, NULL);
    }
    for (uint32_t bit = 0u; bit < OOL_WIRE * 8u; ++bit) {
        g_step = 1000u + bit;
        memcpy(mut, ool, OOL_IMAGE);
        flip(mut, bit);
        (void)attempt_send(mut, OOL_IMAGE, 0u, OOL_WIRE, EITHER, NULL);
    }
    /* Seeded 2..4-bit flips across the wire and the OOL payload. */
    for (unsigned step = 0u; step < FLIP_STEPS; ++step) {
        unsigned flips = 2u + (unsigned)below(3u);
        g_step = 2000u + step;
        memcpy(mut, ool, OOL_IMAGE);
        for (unsigned k = 0u; k < flips; ++k) flip(mut, below(OOL_IMAGE * 8u));
        (void)attempt_send(mut, OOL_IMAGE, 0u, OOL_WIRE, EITHER, NULL);
    }
}

int main(void) {
    fixture names;
    begin("setup", 0u);
    fx_open(&names);
    g_names = names;
    g_names.s = NULL;
    fx_close(&names);
    g_names_ready = 1;
    bases_accepted();
    receive_caps();
    size_mismatch();
    descriptor_counts();
    ool_span_edges();
    unaligned_offsets();
    truncation_sweep(UINT32_C(0x0c0ffee1));
    truncation_sweep(UINT32_C(0x5eed2027));
    bitflip_sweep(UINT32_C(0x0badf00d));
    bitflip_sweep(UINT32_C(0x1badb002));
    printf("guest mach mutation tests passed: %lu sends, %lu failed "
           "closed, %lu accepted\n",
           g_stats.attempts, g_stats.rejected, g_stats.accepted);
    return 0;
}
