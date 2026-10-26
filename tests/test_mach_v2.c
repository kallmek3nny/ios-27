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
int main(void) {
    local_names_and_reply();
    descriptor_and_collision();
    atomic_full_queue_and_validation();
    name_and_right_limits();
    puts("phase 10 task-local Mach namespaces/reply tests passed");
    return 0;
}
