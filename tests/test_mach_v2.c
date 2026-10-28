#include "ios27/mach_v2.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void wr32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0u; i < 4u; ++i) p[i] = (uint8_t)(n >> (8u * i));
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static size_t message(uint8_t p[IOS27_MACH_MSG_MAX], uint32_t remote,
                      uint32_t bits, uint32_t local, uint32_t id) {
    memset(p, 0, IOS27_MACH_MSG_MAX);
    wr32(p, bits);
    wr32(p + 4u, 24u);
    wr32(p + 8u, remote);
    wr32(p + 12u, local);
    wr32(p + 20u, id);
    return 24u;
}
static void descriptor(uint8_t p[IOS27_MACH_MSG_MAX], uint32_t name,
                       uint8_t disposition) {
    wr32(p, IOS27_MACH_COMPLEX | IOS27_MACH_COPY_SEND);
    wr32(p + 4u, 40u);
    wr32(p + 24u, 1u);
    wr32(p + 28u, name);
    p[38u] = disposition;
    p[39u] = IOS27_MACH_PORT_DESCRIPTOR;
}
static void check(const ios27_mach_v2 *s, uint32_t task, uint32_t name,
                  uint32_t sends, uint32_t once, unsigned receive) {
    uint32_t a=0u, b=0u;
    unsigned c=0u;
    assert(ios27_mach_v2_rights(s, task, name, &a, &b, &c) == IOS27_MACH_OK);
    assert(a == sends && b == once && c == receive);
}
static void local_names_and_reply(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t server, reply, caller_send, server_once, a, b;
    unsigned can_receive;
    uint8_t bytes[IOS27_MACH_MSG_MAX], received[IOS27_MACH_MSG_MAX];
    size_t n, capacity;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &server) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, 2u, &reply) == IOS27_MACH_OK);
    assert(server == reply); /* task-local name 100 maps to DIFFERENT ports */
    n = message(bytes, reply, IOS27_MACH_COPY_SEND, 0u, 2u);
    assert(ios27_mach_v2_send(s, 2u, bytes, n, 0u) == IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_make_send(s, 1u, server, 2u, &caller_send) ==
           IOS27_MACH_OK);
    assert(caller_send != reply && caller_send != server);
    check(s, 2u, caller_send, 1u, 0u, 0u);
    check(s, 1u, server, 0u, 0u, 1u);
    n = message(bytes, caller_send,
                IOS27_MACH_COPY_SEND | (IOS27_MACH_MAKE_SEND_ONCE << 8u),
                reply, 2027u);
    assert(ios27_mach_v2_send(s, 2u, bytes, n, 0u) == IOS27_MACH_OK);
    /* Sending only grants a *pending* reply right, never usable early. */
    assert(ios27_mach_v2_send(s, 1u, bytes, n, 0u) ==
           IOS27_MACH_BAD_RIGHT);
    capacity = n - 1u;
    memset(received, 0xa5, sizeof(received));
    assert(ios27_mach_v2_receive(s, 1u, server, received, &capacity, 0u) ==
           IOS27_MACH_TOO_LARGE);
    assert(capacity == n && received[0] == 0xa5u);
    capacity = sizeof(received);
    /* Number 100 belongs to task 2's different, currently empty port. */
    assert(ios27_mach_v2_receive(s, 2u, server, received, &capacity, 0u) ==
           IOS27_MACH_TIMEOUT);
    assert(ios27_mach_v2_receive(s, 1u, server, received, &capacity, 0u) ==
           IOS27_MACH_OK);
    server_once = rd32(received + 8u);
    assert(server_once != 0u && server_once != server);
    assert(rd32(received + 12u) == server);
    assert(rd32(received + 20u) == 2027u);
    check(s, 1u, server_once, 0u, 1u, 0u);
    n = message(bytes, server_once, IOS27_MACH_MOVE_SEND_ONCE, 0u, 99u);
    assert(ios27_mach_v2_send(s, 1u, bytes, n, 0u) == IOS27_MACH_OK);
    assert(ios27_mach_v2_rights(s, 1u, server_once,
                                 &a, &b, &can_receive) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_send(s, 1u, bytes, n, 0u) ==
           IOS27_MACH_BAD_RIGHT);
    capacity = sizeof(received);
    assert(ios27_mach_v2_receive(s, 2u, reply, received, &capacity, 0u) ==
           IOS27_MACH_OK);
    assert(rd32(received + 8u) == 0u);
    assert(rd32(received + 12u) == reply && rd32(received + 20u) == 99u);
    assert(ios27_mach_v2_drop_send_once(s, 1u, server_once) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_drop_send(s, 2u, caller_send) == IOS27_MACH_OK);
    assert(ios27_mach_v2_send(s, 2u, bytes, n, 0u) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_destroy(s, 1u, server) == IOS27_MACH_OK);
    assert(ios27_mach_v2_destroy(s, 2u, reply) == IOS27_MACH_OK);
    uint32_t fresh;
    assert(ios27_mach_v2_allocate(s, 2u, &fresh) == IOS27_MACH_OK);
    assert(fresh != reply); /* stale aliases never recycle */
    ios27_mach_v2_free(s);
}
static void descriptor_and_collision(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t service, item, remote_service, remote_item, received_item;
    uint8_t p[IOS27_MACH_MSG_MAX], out[IOS27_MACH_MSG_MAX];
    size_t len, cap;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &service) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, 3u, &item) == IOS27_MACH_OK);
    assert(service == item); /* same integer, different underlying objects */
    assert(ios27_mach_v2_make_send(s, 1u, service, 2u, &remote_service) ==
           IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 3u, item, 2u, &remote_item) ==
           IOS27_MACH_OK);
    assert(remote_item != remote_service);
    len = message(p, remote_service, IOS27_MACH_COPY_SEND, 0u, 555u);
    descriptor(p, remote_item, IOS27_MACH_MOVE_SEND);
    len = 40u;
    assert(ios27_mach_v2_send(s, 2u, p, len, 0u) == IOS27_MACH_OK);
    {
        uint32_t a, b;
        unsigned c;
        assert(ios27_mach_v2_rights(s, 2u, remote_item,
                                     &a, &b, &c) == IOS27_MACH_BAD_RIGHT);
    }
    assert(ios27_mach_v2_destroy(s, 3u, item) == IOS27_MACH_BAD_RIGHT);
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 1u, service, out, &cap, 0u) ==
           IOS27_MACH_OK);
    assert(rd32(out) == IOS27_MACH_COMPLEX);
    received_item = rd32(out + 28u);
    assert(received_item != service); /* its number can coincide with a name in another task */
    check(s, 1u, received_item, 1u, 0u, 0u);
    assert(ios27_mach_v2_drop_send(s, 1u, received_item) == IOS27_MACH_OK);
    assert(ios27_mach_v2_destroy(s, 3u, item) == IOS27_MACH_OK);

    assert(ios27_mach_v2_allocate(s, 3u, &item) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 3u, item, 2u, &remote_item) ==
           IOS27_MACH_OK);
    len = message(p, remote_service, IOS27_MACH_COPY_SEND, 0u, 88u);
    descriptor(p, remote_item, IOS27_MACH_COPY_SEND);
    assert(ios27_mach_v2_send(s, 2u, p, 40u, 0u) == IOS27_MACH_OK);
    check(s, 2u, remote_item, 1u, 0u, 0u);
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 1u, service, out, &cap, 0u) ==
           IOS27_MACH_OK);
    received_item = rd32(out + 28u);
    check(s, 1u, received_item, 1u, 0u, 0u);
    check(s, 2u, remote_item, 1u, 0u, 0u);
    ios27_mach_v2_free(s);
}
static void atomic_full_queue_and_validation(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t inbox, reply, send_name, once_name;
    uint8_t p[IOS27_MACH_MSG_MAX], out[IOS27_MACH_MSG_MAX];
    size_t n, cap;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &inbox) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, 2u, &reply) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, inbox, 2u, &send_name) ==
           IOS27_MACH_OK);
    n = message(p, send_name, IOS27_MACH_COPY_SEND, 0u, 7u);
    for (unsigned i = 0u; i < IOS27_MACH_QUEUE_MAX; ++i)
        assert(ios27_mach_v2_send(s, 2u, p, n, 0u) == IOS27_MACH_OK);
    n = message(p, send_name, IOS27_MACH_COPY_SEND |
                (IOS27_MACH_MAKE_SEND_ONCE << 8u), reply, 8u);
    assert(ios27_mach_v2_send(s, 2u, p, n, 0u) ==
           IOS27_MACH_TIMEOUT);
    {
        uint32_t x=0u, y=0u;
        unsigned z=0u;
        assert(ios27_mach_v2_rights(s, 1u, inbox, &x, &y, &z) ==
               IOS27_MACH_OK);
        assert(y == 0u);
    }
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 1u, inbox, out, &cap, 0u) ==
           IOS27_MACH_OK);
    assert(ios27_mach_v2_send(s, 2u, p, n, 0u) == IOS27_MACH_OK);
    for (unsigned i = 0u; i < IOS27_MACH_QUEUE_MAX - 1u; ++i) {
        cap = sizeof(out);
        assert(ios27_mach_v2_receive(s, 1u, inbox, out, &cap, 0u) ==
               IOS27_MACH_OK);
    }
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 1u, inbox, out, &cap, 0u) ==
           IOS27_MACH_OK);
    once_name = rd32(out + 8u);
    check(s, 1u, once_name, 0u, 1u, 0u);

    n = message(p, once_name, IOS27_MACH_MOVE_SEND_ONCE, 0u, 77u);
    wr32(p + 16u, 99u); /* malformed voucher must not use right */
    assert(ios27_mach_v2_send(s, 1u, p, n, 0u) ==
           IOS27_MACH_UNSUPPORTED);
    check(s, 1u, once_name, 0u, 1u, 0u);
    wr32(p + 16u, 0u);
    assert(ios27_mach_v2_send(s, 1u, p, n, 0u) == IOS27_MACH_OK);
    assert(ios27_mach_v2_send(s, 1u, p, n, 0u) ==
           IOS27_MACH_BAD_RIGHT);

    n = message(p, send_name, IOS27_MACH_COPY_SEND, 0u, 42u);
    descriptor(p, 9999u, IOS27_MACH_MOVE_SEND);
    assert(ios27_mach_v2_send(s, 2u, p, 40u, 0u) ==
           IOS27_MACH_BAD_RIGHT);
    wr32(p + 28u, send_name);
    p[39u] = 1u;
    assert(ios27_mach_v2_send(s, 2u, p, 40u, 0u) ==
           IOS27_MACH_UNSUPPORTED);
    p[39u] = IOS27_MACH_PORT_DESCRIPTOR;
    wr32(p + 24u, 2u);
    assert(ios27_mach_v2_send(s, 2u, p, 40u, 0u) ==
           IOS27_MACH_UNSUPPORTED);
    check(s, 2u, send_name, 1u, 0u, 0u);
    ios27_mach_v2_free(s);
}
static void name_and_right_limits(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t port, recipient_name, original, next;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &port) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 2u, port, 2u, &recipient_name) ==
           IOS27_MACH_BAD_RIGHT);
    for (unsigned i = 0u; i < IOS27_MACH_RIGHT_MAX; ++i)
        assert(ios27_mach_v2_make_send(s, 1u, port, 2u, &recipient_name) ==
               IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, port, 2u, &recipient_name) ==
           IOS27_MACH_LIMIT);
    check(s, 2u, recipient_name, IOS27_MACH_RIGHT_MAX, 0u, 0u);
    assert(ios27_mach_v2_drop_send(s, 2u, recipient_name) ==
           IOS27_MACH_OK);
    check(s, 2u, recipient_name, IOS27_MACH_RIGHT_MAX - 1u, 0u, 0u);
    assert(ios27_mach_v2_destroy(s, 2u, recipient_name) ==
           IOS27_MACH_BAD_RIGHT);
    original = port;
    assert(ios27_mach_v2_destroy(s, 1u, port) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, 1u, &next) == IOS27_MACH_OK);
    assert(next != original);
    assert(ios27_mach_v2_make_send(s, 1u, original, 2u, &recipient_name) ==
           IOS27_MACH_BAD_RIGHT);
    ios27_mach_v2_free(s);
}

/* In-flight credits in the port being destroyed are canceled, not treated
 * as foreign grants. Cover simultaneous reply and send descriptor credits. */
static void destroy_queued_self_grants(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t port, send_name, remote, fresh;
    uint8_t p[IOS27_MACH_MSG_MAX];
    size_t n;
    uint32_t sends, once;
    unsigned receive;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &port) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, port, 1u, &send_name) ==
           IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, port, 2u, &remote) ==
           IOS27_MACH_OK);
    n = message(p, send_name, IOS27_MACH_COPY_SEND |
                (IOS27_MACH_MAKE_SEND_ONCE << 8u), port, 111u);
    descriptor(p, send_name, IOS27_MACH_COPY_SEND);
    wr32(p, IOS27_MACH_COMPLEX | IOS27_MACH_COPY_SEND |
         (IOS27_MACH_MAKE_SEND_ONCE << 8u));
    n = 40u;
    assert(ios27_mach_v2_send(s, 1u, p, n, 0u) == IOS27_MACH_OK);
    /* Previously this was rejected by a pending credit in its own queue. */
    assert(ios27_mach_v2_destroy(s, 1u, port) == IOS27_MACH_OK);
    assert(ios27_mach_v2_rights(s, 1u, port, &sends, &once, &receive) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_rights(s, 1u, send_name, &sends, &once, &receive) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_rights(s, 2u, remote, &sends, &once, &receive) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_allocate(s, 1u, &fresh) == IOS27_MACH_OK);
    assert(fresh != port && fresh != send_name);
    assert(ios27_mach_v2_make_send(s, 1u, port, 2u, &remote) ==
           IOS27_MACH_BAD_RIGHT);
    ios27_mach_v2_free(s);
}

static void teardown_queued_multiport_grants(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t inbox, target, send_inbox, send_target, other_name;
    uint8_t p[IOS27_MACH_MSG_MAX];
    uint32_t sends, once;
    unsigned receive;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &inbox) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, 1u, &target) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, inbox, 1u, &send_inbox) ==
           IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, target, 1u, &send_target) ==
           IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, target, 2u, &other_name) ==
           IOS27_MACH_OK);
    (void)message(p, send_inbox, IOS27_MACH_COPY_SEND |
                  (IOS27_MACH_MAKE_SEND_ONCE << 8u), target, 222u);
    descriptor(p, send_target, IOS27_MACH_MOVE_SEND);
    wr32(p, IOS27_MACH_COMPLEX | IOS27_MACH_COPY_SEND |
         (IOS27_MACH_MAKE_SEND_ONCE << 8u));
    assert(ios27_mach_v2_send(s, 1u, p, 40u, 0u) == IOS27_MACH_OK);
    assert(ios27_mach_v2_task_teardown(s, 1u) == IOS27_MACH_OK);
    assert(ios27_mach_v2_rights(s, 2u, other_name,
                                 &sends, &once, &receive) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_task_teardown(s, 1u) == IOS27_MACH_OK);
    ios27_mach_v2_free(s);
}

/* The destination task must never cancel credits reserved in a foreign
 * live queue. Failed teardown must preserve all rights and queued data. */
static void teardown_foreign_queue_is_atomic(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t target, inbox, target_in_task2, send_inbox, received;
    uint8_t p[IOS27_MACH_MSG_MAX], out[IOS27_MACH_MSG_MAX];
    size_t cap;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &target) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, 2u, &inbox) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, target, 2u,
                                   &target_in_task2) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 2u, inbox, 2u, &send_inbox) ==
           IOS27_MACH_OK);
    (void)message(p, send_inbox, IOS27_MACH_COPY_SEND, 0u, 333u);
    descriptor(p, target_in_task2, IOS27_MACH_MOVE_SEND);
    assert(ios27_mach_v2_send(s, 2u, p, 40u, 0u) == IOS27_MACH_OK);
    assert(ios27_mach_v2_task_teardown(s, 1u) == IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_destroy(s, 1u, target) == IOS27_MACH_BAD_RIGHT);
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 2u, inbox, out, &cap, 0u) ==
           IOS27_MACH_OK);
    assert(cap == 40u && rd32(out + 20u) == 333u);
    received = rd32(out + 28u);
    check(s, 2u, received, 1u, 0u, 0u);
    assert(ios27_mach_v2_task_teardown(s, 1u) == IOS27_MACH_OK);
    assert(ios27_mach_v2_drop_send(s, 2u, received) ==
           IOS27_MACH_BAD_RIGHT);
    ios27_mach_v2_free(s);
}

static void deterministic_port_exhaustion(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t names[IOS27_MACH_PORT_MAX], owners[IOS27_MACH_PORT_MAX];
    uint32_t replacement = 0u;
    assert(s);
    for (uint32_t i = 0u; i < IOS27_MACH_PORT_MAX; ++i) {
        owners[i] = 1u + i % IOS27_MACH_TASK_MAX;
        assert(ios27_mach_v2_allocate(s, owners[i], &names[i]) ==
               IOS27_MACH_OK);
    }
    assert(ios27_mach_v2_allocate(s, 1u, &replacement) == IOS27_MACH_LIMIT);
    assert(ios27_mach_v2_destroy(s, owners[0], names[0]) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, owners[0], &replacement) ==
           IOS27_MACH_OK);
    assert(replacement != names[0]);
    assert(ios27_mach_v2_destroy(s, owners[0], names[0]) ==
           IOS27_MACH_BAD_RIGHT);
    for (uint32_t i = 1u; i < IOS27_MACH_PORT_MAX; ++i)
        assert(ios27_mach_v2_destroy(s, owners[i], names[i]) ==
               IOS27_MACH_OK);
    assert(ios27_mach_v2_destroy(s, owners[0], replacement) ==
           IOS27_MACH_OK);
    ios27_mach_v2_free(s);
}

static void malformed_wire_stress(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t port, sender;
    uint8_t p[IOS27_MACH_MSG_MAX], out[IOS27_MACH_MSG_MAX];
    size_t cap;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &port) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, port, 2u, &sender) ==
           IOS27_MACH_OK);
    for (uint32_t i = 0u; i < 512u; ++i) {
        size_t n = message(p, sender, IOS27_MACH_COPY_SEND, 0u, i);
        switch (i % 4u) {
            case 0u: wr32(p + 16u, i + 1u); break;
            case 1u: wr32(p + 4u, 28u); break;
            case 2u: wr32(p, IOS27_MACH_COPY_SEND | UINT32_C(0x10000)); break;
            default: wr32(p + 8u, sender + 10000u); break;
        }
        assert(ios27_mach_v2_send(s, 2u, p, n, 0u) != IOS27_MACH_OK);
        check(s, 2u, sender, 1u, 0u, 0u);
        cap = sizeof(out);
        assert(ios27_mach_v2_receive(s, 1u, port, out, &cap, 0u) ==
               IOS27_MACH_TIMEOUT);
    }
    ios27_mach_v2_free(s);
}


/* Phase 14B: a remote MOVE_SEND and an inline MOVE_SEND can debit the
 * exact same local right. Both debits and queue insertion are atomic. */
static void move_send_combined_debits(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t server, alias;
    uint8_t p[IOS27_MACH_MSG_MAX], out[IOS27_MACH_MSG_MAX];
    size_t cap;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &server) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, server, 2u, &alias) ==
           IOS27_MACH_OK);
    message(p, alias, IOS27_MACH_MOVE_SEND, 0u, 1001u);
    descriptor(p, alias, IOS27_MACH_MOVE_SEND);
    wr32(p, IOS27_MACH_COMPLEX | IOS27_MACH_MOVE_SEND);
    assert(ios27_mach_v2_send(s, 2u, p, 40u, 0u) ==
           IOS27_MACH_BAD_RIGHT);
    check(s, 2u, alias, 1u, 0u, 0u);
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 1u, server, out, &cap, 0u) ==
           IOS27_MACH_TIMEOUT);
    assert(ios27_mach_v2_make_send(s, 1u, server, 2u, &alias) ==
           IOS27_MACH_OK);
    check(s, 2u, alias, 2u, 0u, 0u);
    assert(ios27_mach_v2_send(s, 2u, p, 40u, 0u) == IOS27_MACH_OK);
    {
        uint32_t a, b;
        unsigned receive;
        assert(ios27_mach_v2_rights(s, 2u, alias, &a, &b, &receive) ==
               IOS27_MACH_BAD_RIGHT);
    }
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 1u, server, out, &cap, 0u) ==
           IOS27_MACH_OK);
    assert(cap == 40u && rd32(out + 20u) == 1001u);
    assert(rd32(out) == IOS27_MACH_COMPLEX);
    assert(rd32(out + 28u) == server);
    check(s, 1u, server, 1u, 0u, 1u);
    assert(ios27_mach_v2_drop_send(s, 1u, server) == IOS27_MACH_OK);
    assert(ios27_mach_v2_destroy(s, 1u, server) == IOS27_MACH_OK);
    ios27_mach_v2_free(s);
}

static void move_send_queue_rollback_and_wrap(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t server, alias;
    uint8_t p[IOS27_MACH_MSG_MAX], out[IOS27_MACH_MSG_MAX];
    size_t cap;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &server) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, server, 2u, &alias) ==
           IOS27_MACH_OK);
    for (unsigned i = 0u; i < IOS27_MACH_QUEUE_MAX; ++i) {
        message(p, alias, IOS27_MACH_COPY_SEND, 0u, i);
        assert(ios27_mach_v2_send(s, 2u, p, 24u, 0u) == IOS27_MACH_OK);
    }
    message(p, alias, IOS27_MACH_MOVE_SEND, 0u, 9001u);
    assert(ios27_mach_v2_send(s, 2u, p, 24u, 0u) ==
           IOS27_MACH_TIMEOUT);
    check(s, 2u, alias, 1u, 0u, 0u);
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 1u, server, out, &cap, 0u) ==
           IOS27_MACH_OK);
    assert(rd32(out + 20u) == 0u);
    assert(ios27_mach_v2_send(s, 2u, p, 24u, 0u) == IOS27_MACH_OK);
    for (unsigned i = 1u; i < IOS27_MACH_QUEUE_MAX; ++i) {
        cap = sizeof(out);
        assert(ios27_mach_v2_receive(s, 1u, server, out, &cap, 0u) ==
               IOS27_MACH_OK);
        assert(rd32(out + 20u) == i);
    }
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 1u, server, out, &cap, 0u) ==
           IOS27_MACH_OK);
    assert(rd32(out + 20u) == 9001u && rd32(out) == 0u);
    assert(ios27_mach_v2_send(s, 2u, p, 24u, 0u) ==
           IOS27_MACH_BAD_RIGHT);
    ios27_mach_v2_free(s);
}

static void teardown_and_exhaustion_stress(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t service, reply, send_name, fresh;
    uint32_t ports[IOS27_MACH_PORT_MAX];
    uint8_t p[IOS27_MACH_MSG_MAX], out[IOS27_MACH_MSG_MAX];
    size_t cap;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &service) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, 2u, &reply) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, service, 2u, &send_name) ==
           IOS27_MACH_OK);
    message(p, send_name, IOS27_MACH_COPY_SEND |
            (IOS27_MACH_MAKE_SEND_ONCE << 8u), reply, 2027u);
    assert(ios27_mach_v2_send(s, 2u, p, 24u, 0u) == IOS27_MACH_OK);
    /* Blocked teardown never consumes the queue or pending credits. */
    for (unsigned i = 0u; i < 32u; ++i)
        assert(ios27_mach_v2_task_teardown(s, 1u) ==
               IOS27_MACH_BAD_RIGHT);
    check(s, 2u, send_name, 1u, 0u, 0u);
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 1u, service, out, &cap, 0u) ==
           IOS27_MACH_OK);
    assert(rd32(out) == IOS27_MACH_MOVE_SEND_ONCE);
    assert(rd32(out + 20u) == 2027u);
    assert(ios27_mach_v2_task_teardown(s, 1u) == IOS27_MACH_OK);
    assert(ios27_mach_v2_send(s, 2u, p, 24u, 0u) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_task_teardown(s, 2u) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, 2u, &fresh) == IOS27_MACH_OK);
    assert(fresh != reply);
    assert(ios27_mach_v2_receive(s, 2u, reply, out, &cap, 0u) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_destroy(s, 2u, fresh) == IOS27_MACH_OK);
    for (unsigned i = 0u; i < IOS27_MACH_PORT_MAX; ++i)
        assert(ios27_mach_v2_allocate(s, (i % IOS27_MACH_TASK_MAX) + 1u,
                                      &ports[i]) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, 1u, &fresh) == IOS27_MACH_LIMIT);
    assert(ios27_mach_v2_make_send(s, 1u, ports[0], 8u, &send_name) ==
           IOS27_MACH_OK);
    assert(ios27_mach_v2_destroy(s, 1u, ports[0]) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, 1u, &fresh) == IOS27_MACH_OK);
    assert(fresh != ports[0]);
    message(p, send_name, IOS27_MACH_COPY_SEND, 0u, 123u);
    assert(ios27_mach_v2_send(s, 8u, p, 24u, 0u) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_make_send(s, 1u, ports[0], 8u, &send_name) ==
           IOS27_MACH_BAD_RIGHT);
    ios27_mach_v2_free(s);
}

static void rejected_wire_fuzz_is_side_effect_free(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t server, sender, state = UINT32_C(0x1d4b09ee);
    uint8_t p[IOS27_MACH_MSG_MAX], out[IOS27_MACH_MSG_MAX];
    size_t cap;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &server) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, server, 2u, &sender) ==
           IOS27_MACH_OK);
    for (unsigned i = 0u; i < 512u; ++i) {
        size_t n = message(p, sender, IOS27_MACH_MOVE_SEND, 0u, i);
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        switch (i % 4u) {
        case 0u: wr32(p + 16u, state | 1u); break;
        case 1u: wr32(p + 4u, 28u); break;
        case 2u: wr32(p, IOS27_MACH_MOVE_SEND | UINT32_C(0x10000)); break;
        default: n = 23u; break;
        }
        assert(ios27_mach_v2_send(s, 2u, p, n, 0u) != IOS27_MACH_OK);
        check(s, 2u, sender, 1u, 0u, 0u);
    }
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 1u, server, out, &cap, 0u) ==
           IOS27_MACH_TIMEOUT);
    message(p, sender, IOS27_MACH_MOVE_SEND, 0u, 4242u);
    assert(ios27_mach_v2_send(s, 2u, p, 24u, 0u) == IOS27_MACH_OK);
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 1u, server, out, &cap, 0u) ==
           IOS27_MACH_OK);
    assert(rd32(out + 20u) == 4242u);
    ios27_mach_v2_free(s);
}

/* Phase 16C: forward a real pending reply capability across an intermediate
 * synthetic task; queue exhaustion and malformed headers never spend it. */
static void forward_send_once_three_tasks(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t service, relay, reply, to_service, to_relay, once, forwarded;
    uint8_t p[IOS27_MACH_MSG_MAX], out[IOS27_MACH_MSG_MAX];
    size_t cap;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &service) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, 2u, &relay) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, 3u, &reply) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, service, 3u, &to_service) ==
           IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 2u, relay, 1u, &to_relay) ==
           IOS27_MACH_OK);
    message(p, to_service, IOS27_MACH_COPY_SEND |
            (IOS27_MACH_MAKE_SEND_ONCE << 8u), reply, 101u);
    assert(ios27_mach_v2_send(s, 3u, p, 24u, 0u) == IOS27_MACH_OK);
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 1u, service, out, &cap, 0u) ==
           IOS27_MACH_OK);
    once = rd32(out + 8u);
    check(s, 1u, once, 0u, 1u, 0u);

    for (uint32_t i = 0u; i < IOS27_MACH_QUEUE_MAX; ++i) {
        message(p, to_relay, IOS27_MACH_COPY_SEND, 0u, i + 10u);
        assert(ios27_mach_v2_send(s, 1u, p, 24u, 0u) == IOS27_MACH_OK);
    }
    message(p, to_relay, IOS27_MACH_COPY_SEND |
            (IOS27_MACH_MOVE_SEND_ONCE << 8u), once, 202u);
    wr32(p + 16u, 1u);
    assert(ios27_mach_v2_send(s, 1u, p, 24u, 0u) ==
           IOS27_MACH_UNSUPPORTED);
    wr32(p + 16u, 0u);
    assert(ios27_mach_v2_send(s, 1u, p, 24u, 0u) ==
           IOS27_MACH_TIMEOUT);
    check(s, 1u, once, 0u, 1u, 0u);
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 2u, relay, out, &cap, 0u) ==
           IOS27_MACH_OK);
    assert(rd32(out + 20u) == 10u);
    assert(ios27_mach_v2_send(s, 1u, p, 24u, 0u) == IOS27_MACH_OK);
    /* The pending foreign-queue transfer blocks owner teardown. */
    assert(ios27_mach_v2_task_teardown(s, 3u) == IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_drop_send_once(s, 1u, once) ==
           IOS27_MACH_BAD_RIGHT);
    for (uint32_t i = 1u; i < IOS27_MACH_QUEUE_MAX; ++i) {
        cap = sizeof(out);
        assert(ios27_mach_v2_receive(s, 2u, relay, out, &cap, 0u) ==
               IOS27_MACH_OK);
        assert(rd32(out + 20u) == i + 10u);
    }
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 2u, relay, out, &cap, 0u) ==
           IOS27_MACH_OK);
    assert(rd32(out + 20u) == 202u);
    forwarded = rd32(out + 8u);
    check(s, 2u, forwarded, 0u, 1u, 0u);
    message(p, forwarded, IOS27_MACH_MOVE_SEND_ONCE, 0u, 303u);
    assert(ios27_mach_v2_send(s, 2u, p, 24u, 0u) == IOS27_MACH_OK);
    assert(ios27_mach_v2_send(s, 2u, p, 24u, 0u) ==
           IOS27_MACH_BAD_RIGHT);
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 3u, reply, out, &cap, 0u) ==
           IOS27_MACH_OK);
    assert(rd32(out + 20u) == 303u && rd32(out + 12u) == reply);
    assert(ios27_mach_v2_destroy(s, 1u, service) == IOS27_MACH_OK);
    assert(ios27_mach_v2_destroy(s, 2u, relay) == IOS27_MACH_OK);
    assert(ios27_mach_v2_destroy(s, 3u, reply) == IOS27_MACH_OK);
    ios27_mach_v2_free(s);
}

/* A single once right cannot be used both as destination and transferred
 * local right. Rejection must not enqueue or consume that right. */
static void once_double_debit_is_atomic(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t service, reply, to_service, once;
    uint8_t p[IOS27_MACH_MSG_MAX], out[IOS27_MACH_MSG_MAX];
    size_t cap;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &service) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, 2u, &reply) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, service, 2u, &to_service) ==
           IOS27_MACH_OK);
    message(p, to_service, IOS27_MACH_COPY_SEND |
            (IOS27_MACH_MAKE_SEND_ONCE << 8u), reply, 404u);
    assert(ios27_mach_v2_send(s, 2u, p, 24u, 0u) == IOS27_MACH_OK);
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 1u, service, out, &cap, 0u) ==
           IOS27_MACH_OK);
    once = rd32(out + 8u);
    message(p, once, IOS27_MACH_MOVE_SEND_ONCE |
            (IOS27_MACH_MOVE_SEND_ONCE << 8u), once, 405u);
    assert(ios27_mach_v2_send(s, 1u, p, 24u, 0u) ==
           IOS27_MACH_BAD_RIGHT);
    check(s, 1u, once, 0u, 1u, 0u);
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 2u, reply, out, &cap, 0u) ==
           IOS27_MACH_TIMEOUT);
    message(p, once, IOS27_MACH_MOVE_SEND_ONCE, 0u, 406u);
    assert(ios27_mach_v2_send(s, 1u, p, 24u, 0u) == IOS27_MACH_OK);
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 2u, reply, out, &cap, 0u) ==
           IOS27_MACH_OK);
    assert(rd32(out + 20u) == 406u);
    ios27_mach_v2_free(s);
}

/* Phase 16C: COPY_SEND transfers a send uref, so the descriptor returned
 * to the receiver must carry MOVE_SEND, not the sender's disposition.
 * Failed validation and TOO_LARGE leave the rights and queue intact. */
static void descriptor_normalization_with_send_once_reply(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t service, item, reply, service_alias, item_alias;
    uint32_t received_item, reply_once;
    uint8_t p[IOS27_MACH_MSG_MAX], out[IOS27_MACH_MSG_MAX];
    size_t cap;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &service) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, 2u, &reply) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, 3u, &item) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, service, 2u, &service_alias) ==
           IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 3u, item, 2u, &item_alias) ==
           IOS27_MACH_OK);
    (void)message(p, service_alias, IOS27_MACH_COPY_SEND, reply, 1600u);
    descriptor(p, item_alias, IOS27_MACH_COPY_SEND);
    wr32(p, IOS27_MACH_COMPLEX | IOS27_MACH_COPY_SEND |
         (IOS27_MACH_MAKE_SEND_ONCE << 8u));
    p[36u] = 1u;
    assert(ios27_mach_v2_send(s, 2u, p, 40u, 0u) ==
           IOS27_MACH_UNSUPPORTED);
    p[36u] = 0u;
    check(s, 2u, item_alias, 1u, 0u, 0u);
    assert(ios27_mach_v2_send(s, 2u, p, 40u, 0u) == IOS27_MACH_OK);
    cap = 39u;
    memset(out, 0x7c, sizeof(out));
    assert(ios27_mach_v2_receive(s, 1u, service, out, &cap, 0u) ==
           IOS27_MACH_TOO_LARGE);
    assert(cap == 40u && out[38u] == 0x7cu);
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 1u, service, out, &cap, 0u) ==
           IOS27_MACH_OK);
    assert(cap == 40u && out[38u] == IOS27_MACH_MOVE_SEND);
    assert(rd32(out) == (IOS27_MACH_COMPLEX | IOS27_MACH_MOVE_SEND_ONCE));
    received_item = rd32(out + 28u);
    reply_once = rd32(out + 8u);
    check(s, 2u, item_alias, 1u, 0u, 0u);
    check(s, 1u, received_item, 1u, 0u, 0u);
    check(s, 1u, reply_once, 0u, 1u, 0u);
    (void)message(p, reply_once, IOS27_MACH_MOVE_SEND_ONCE, 0u, 1601u);
    assert(ios27_mach_v2_send(s, 1u, p, 24u, 0u) == IOS27_MACH_OK);
    assert(ios27_mach_v2_send(s, 1u, p, 24u, 0u) ==
           IOS27_MACH_BAD_RIGHT);
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 2u, reply, out, &cap, 0u) ==
           IOS27_MACH_OK);
    assert(rd32(out + 20u) == 1601u);
    ios27_mach_v2_free(s);
}

/* The raw host fixture must never permit queue bytes or lengths to overwrite
 * one another or its private state, even if the caller forges the pointers. */
static void host_alias_rejection_preserves_queued_message(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t server, sender;
    _Alignas(size_t) uint8_t buffer[IOS27_MACH_MSG_MAX];
    size_t cap;
    uint32_t a, b;
    unsigned receive;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &server) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, server, 2u, &sender) ==
           IOS27_MACH_OK);
    (void)message(buffer, sender, IOS27_MACH_MOVE_SEND, 0u, 1602u);
    assert(ios27_mach_v2_send(s, 2u, (const uint8_t *)(const void *)s,
                              24u, 0u) == IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_v2_send(s, 2u, buffer, 24u, 0u) == IOS27_MACH_OK);
    cap = sizeof(buffer);
    assert(ios27_mach_v2_receive(s, 1u, server, buffer,
                     (size_t *)(void *)buffer, 0u) ==
           IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_v2_receive(s, 1u, server, s, &cap, 0u) ==
           IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_v2_receive(s, 1u, server, buffer,
                     (size_t *)(void *)s, 0u) ==
           IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_v2_rights(s, 2u, sender, &a, &b, &receive) ==
           IOS27_MACH_BAD_RIGHT);
    cap = sizeof(buffer);
    assert(ios27_mach_v2_receive(s, 1u, server, buffer, &cap, 0u) ==
           IOS27_MACH_OK);
    assert(cap == 24u && rd32(buffer + 20u) == 1602u);
    ios27_mach_v2_free(s);
}


/* Phase 17c: distinct objects, duplicate names, mixed COPY/MOVE and
 * send-once replies are one all-or-nothing multi-task transaction. */
static void phase17c_complex_multi_transfer(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t inbox, reply, item, dst, item_alias, received_item, once;
    uint32_t sends, pending_once;
    unsigned receive;
    uint8_t p[IOS27_MACH_MSG_MAX], out[IOS27_MACH_MSG_MAX];
    size_t n, cap;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &inbox) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, 2u, &reply) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, 3u, &item) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, inbox, 2u, &dst) == IOS27_MACH_OK);
    for (unsigned i = 0u; i < 3u; ++i)
        assert(ios27_mach_v2_make_send(s, 3u, item, 2u,
                                       &item_alias) == IOS27_MACH_OK);
    n = message(p, dst, IOS27_MACH_COMPLEX | IOS27_MACH_COPY_SEND |
                (IOS27_MACH_MAKE_SEND_ONCE << 8u), reply, 1701u);
    wr32(p + 4u, 76u);
    wr32(p + 24u, 4u);
    for (unsigned i = 0u; i < 4u; ++i) {
        size_t off = 28u + 12u * i;
        wr32(p + off, item_alias);
        p[off + 10u] = i == 3u ? IOS27_MACH_COPY_SEND :
                                 IOS27_MACH_MOVE_SEND;
        p[off + 11u] = IOS27_MACH_PORT_DESCRIPTOR;
    }
    /* Four descriptors request three debits; preserve the fourth copy. */
    assert(ios27_mach_v2_send(s, 2u, p, 76u, 0u) == IOS27_MACH_OK);
    assert(ios27_mach_v2_rights(s, 2u, item_alias, &sends,
                                 &pending_once, &receive) == IOS27_MACH_BAD_RIGHT);
    cap = 75u;
    memset(out, 0x79, sizeof(out));
    assert(ios27_mach_v2_receive(s, 1u, inbox, out, &cap, 0u) ==
           IOS27_MACH_TOO_LARGE);
    assert(cap == 76u && out[0] == 0x79u);
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 1u, inbox, out, &cap, 0u) ==
           IOS27_MACH_OK);
    assert(rd32(out + 24u) == 4u);
    once = rd32(out + 8u);
    received_item = rd32(out + 28u);
    for (unsigned i = 0u; i < 4u; ++i) {
        size_t off = 28u + 12u * i;
        assert(rd32(out + off) == received_item);
        assert(out[off + 10u] == IOS27_MACH_MOVE_SEND);
    }
    check(s, 1u, received_item, 4u, 0u, 0u);
    check(s, 1u, once, 0u, 1u, 0u);
    n = message(p, once, IOS27_MACH_MOVE_SEND_ONCE, 0u, 1702u);
    assert(ios27_mach_v2_send(s, 1u, p, n, 0u) == IOS27_MACH_OK);
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 2u, reply, out, &cap, 0u) ==
           IOS27_MACH_OK);
    assert(rd32(out + 20u) == 1702u);
    for (unsigned i = 0u; i < 4u; ++i)
        assert(ios27_mach_v2_drop_send(s, 1u, received_item) ==
               IOS27_MACH_OK);
    assert(ios27_mach_v2_destroy(s, 3u, item) == IOS27_MACH_OK);
    ios27_mach_v2_free(s);
}

/* Repeated MOVE_SEND transfers sharing the remote destination require
 * aggregate urefs, and rejected attempts never leak credits or aliases. */
static void phase17c_overlapping_debits_and_discard(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t inbox, remote, fresh, sends, once;
    unsigned receive;
    uint8_t p[IOS27_MACH_MSG_MAX], out[IOS27_MACH_MSG_MAX];
    size_t cap;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &inbox) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, inbox, 2u, &remote) ==
           IOS27_MACH_OK);
    (void)message(p, remote, IOS27_MACH_COMPLEX | IOS27_MACH_MOVE_SEND,
                  0u, 1703u);
    wr32(p + 4u, 64u);
    wr32(p + 24u, 3u);
    for (unsigned i = 0u; i < 3u; ++i) {
        size_t off = 28u + 12u * i;
        wr32(p + off, remote);
        p[off + 10u] = IOS27_MACH_MOVE_SEND;
        p[off + 11u] = IOS27_MACH_PORT_DESCRIPTOR;
    }
    assert(ios27_mach_v2_send(s, 2u, p, 64u, 0u) == IOS27_MACH_BAD_RIGHT);
    check(s, 2u, remote, 1u, 0u, 0u);
    for (unsigned i = 0u; i < 2u; ++i)
        assert(ios27_mach_v2_make_send(s, 1u, inbox, 2u, &remote) ==
               IOS27_MACH_OK);
    assert(ios27_mach_v2_send(s, 2u, p, 64u, 0u) == IOS27_MACH_BAD_RIGHT);
    check(s, 2u, remote, 3u, 0u, 0u);
    assert(ios27_mach_v2_make_send(s, 1u, inbox, 2u, &remote) ==
           IOS27_MACH_OK);
    p[51u] = 99u;
    assert(ios27_mach_v2_send(s, 2u, p, 64u, 0u) ==
           IOS27_MACH_UNSUPPORTED);
    p[51u] = IOS27_MACH_PORT_DESCRIPTOR;
    assert(ios27_mach_v2_send(s, 2u, p, 64u, 0u) == IOS27_MACH_OK);
    assert(ios27_mach_v2_rights(s, 2u, remote, &sends, &once, &receive) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_destroy(s, 1u, inbox) == IOS27_MACH_OK);
    assert(ios27_mach_v2_receive(s, 1u, inbox, out, &cap, 0u) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_allocate(s, 1u, &fresh) == IOS27_MACH_OK);
    assert(fresh != inbox);
    ios27_mach_v2_free(s);
}

/* Pending credits in a foreign-owned object must block destruction and
 * task retirement until the destination queue consumes or discards them. */
static void phase17c_multitask_rollback(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t server, item, dest, alias, delivered, a, b;
    unsigned receive;
    uint8_t p[IOS27_MACH_MSG_MAX], out[IOS27_MACH_MSG_MAX];
    size_t cap;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &server) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, 3u, &item) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, server, 2u, &dest) ==
           IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 3u, item, 2u, &alias) ==
           IOS27_MACH_OK);
    (void)message(p, dest, IOS27_MACH_COMPLEX | IOS27_MACH_COPY_SEND,
                  0u, 1704u);
    wr32(p + 4u, 52u);
    wr32(p + 24u, 2u);
    for (unsigned i = 0u; i < 2u; ++i) {
        size_t off = 28u + 12u * i;
        wr32(p + off, alias);
        p[off + 10u] = IOS27_MACH_COPY_SEND;
        p[off + 11u] = IOS27_MACH_PORT_DESCRIPTOR;
    }
    assert(ios27_mach_v2_send(s, 2u, p, 52u, 0u) == IOS27_MACH_OK);
    assert(ios27_mach_v2_destroy(s, 3u, item) == IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_task_teardown(s, 1u) == IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_rights(s, 2u, dest, &a, &b, &receive) ==
           IOS27_MACH_OK && a == 1u);
    cap = sizeof(out);
    assert(ios27_mach_v2_receive(s, 1u, server, out, &cap, 0u) ==
           IOS27_MACH_OK);
    delivered = rd32(out + 28u);
    check(s, 1u, delivered, 2u, 0u, 0u);
    assert(ios27_mach_v2_destroy(s, 3u, item) == IOS27_MACH_OK);
    assert(ios27_mach_v2_task_teardown(s, 1u) == IOS27_MACH_OK);
    ios27_mach_v2_free(s);
}


/* Dead-name observers are task-local, FIFO, and never see a recycled name.
 * A full queue rejects destruction without changing the live port. */
static void phase17c_dead_name_capacity_and_lifecycle(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t owner, alias = 0u, dead = 0u, old[IOS27_MACH_V2_NAMES];
    uint32_t a, b;
    unsigned r;
    assert(s);
    for (unsigned i = 0u; i < IOS27_MACH_V2_NAMES; ++i) {
        assert(ios27_mach_v2_allocate(s, 3u, &owner) == IOS27_MACH_OK);
        assert(ios27_mach_v2_make_send(s, 3u, owner, 2u, &alias) ==
               IOS27_MACH_OK);
        assert(ios27_mach_v2_watch_dead_name(s, 2u, alias) == IOS27_MACH_OK);
        old[i] = alias;
        assert(ios27_mach_v2_destroy(s, 3u, owner) == IOS27_MACH_OK);
        assert(ios27_mach_v2_rights(s, 2u, alias, &a, &b, &r) ==
               IOS27_MACH_BAD_RIGHT);
    }
    assert(ios27_mach_v2_allocate(s, 3u, &owner) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 3u, owner, 2u, &alias) ==
           IOS27_MACH_OK);
    assert(ios27_mach_v2_watch_dead_name(s, 2u, alias) == IOS27_MACH_OK);
    assert(ios27_mach_v2_destroy(s, 3u, owner) == IOS27_MACH_LIMIT);
    check(s, 2u, alias, 1u, 0u, 0u);
    assert(ios27_mach_v2_poll_dead_name(s, 2u, &dead) == IOS27_MACH_OK);
    assert(dead == old[0]);
    assert(ios27_mach_v2_destroy(s, 3u, owner) == IOS27_MACH_OK);
    for (unsigned i = 1u; i < IOS27_MACH_V2_NAMES; ++i) {
        assert(ios27_mach_v2_poll_dead_name(s, 2u, &dead) ==
               IOS27_MACH_OK);
        assert(dead == old[i]);
    }
    assert(ios27_mach_v2_poll_dead_name(s, 2u, &dead) == IOS27_MACH_OK);
    assert(dead == alias);
    dead = 0xbeefu;
    assert(ios27_mach_v2_poll_dead_name(s, 2u, &dead) ==
           IOS27_MACH_TIMEOUT && dead == 0xbeefu);
    ios27_mach_v2_free(s);
}

/* Multi-port teardown preflights foreign observers and discards the
 * terminating task's own unread notifications. */
static void phase17c_dead_name_multitask_teardown(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t x, y, sx, sy, event;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &x) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, 1u, &y) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, x, 2u, &sx) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, y, 2u, &sy) == IOS27_MACH_OK);
    assert(ios27_mach_v2_watch_dead_name(s, 2u, sx) == IOS27_MACH_OK);
    assert(ios27_mach_v2_watch_dead_name(s, 2u, sy) == IOS27_MACH_OK);
    assert(ios27_mach_v2_watch_dead_name(s, 2u, 0u) == IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_task_teardown(s, 1u) == IOS27_MACH_OK);
    assert(ios27_mach_v2_poll_dead_name(s, 2u, &event) == IOS27_MACH_OK);
    assert(event == sx);
    assert(ios27_mach_v2_poll_dead_name(s, 2u, &event) == IOS27_MACH_OK);
    assert(event == sy);
    assert(ios27_mach_v2_poll_dead_name(s, 1u, &event) == IOS27_MACH_TIMEOUT);
    assert(ios27_mach_v2_poll_dead_name(s, 2u, &event) == IOS27_MACH_TIMEOUT);
    ios27_mach_v2_free(s);
}

int main(void) {
    phase17c_dead_name_capacity_and_lifecycle();
    phase17c_dead_name_multitask_teardown();
    phase17c_complex_multi_transfer();
    phase17c_overlapping_debits_and_discard();
    phase17c_multitask_rollback();
    local_names_and_reply();
    descriptor_and_collision();
    atomic_full_queue_and_validation();
    name_and_right_limits();
    destroy_queued_self_grants();
    teardown_queued_multiport_grants();
    teardown_foreign_queue_is_atomic();
    deterministic_port_exhaustion();
    malformed_wire_stress();
    move_send_combined_debits();
    move_send_queue_rollback_and_wrap();
    teardown_and_exhaustion_stress();
    rejected_wire_fuzz_is_side_effect_free();
    descriptor_normalization_with_send_once_reply();
    host_alias_rejection_preserves_queued_message();
    forward_send_once_three_tasks();
    once_double_debit_is_atomic();
    puts("phase 17c mach multi-descriptor atomicity tests passed");
    return 0;
}
