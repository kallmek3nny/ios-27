#include "ios27/darwin_mach_abi.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void wr32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0u; i < 4u; ++i)
        p[i] = (uint8_t)(value >> (8u * i));
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static void message(uint8_t *p, uint32_t dest, uint32_t bits,
                    uint32_t local, uint32_t id) {
    memset(p, 0, 24u);
    wr32(p, bits);
    wr32(p + 4u, 24u);
    wr32(p + 8u, dest);
    wr32(p + 12u, local);
    wr32(p + 20u, id);
}
static void complex(uint8_t *p, uint32_t dest, uint32_t transferred) {
    memset(p, 0, 40u);
    wr32(p, IOS27_MACH_COMPLEX | IOS27_MACH_COPY_SEND);
    wr32(p + 4u, 40u);
    wr32(p + 8u, dest);
    wr32(p + 24u, 1u);
    wr32(p + 28u, transferred);
    p[38u] = IOS27_MACH_MOVE_SEND;
    p[39u] = IOS27_MACH_PORT_DESCRIPTOR;
}
static void teardown_inflight(void) {
    ios27_mach_v2 *space = ios27_mach_v2_new();
    uint32_t inbox = 0u, item = 0u, send_inbox = 0u, send_item = 0u;
    uint8_t packet[IOS27_MACH_MSG_MAX], out[IOS27_MACH_MSG_MAX];
    size_t size = sizeof(out);
    uint32_t a, b;
    unsigned receive;
    assert(space);
    assert(ios27_mach_v2_allocate(space, 1u, &inbox) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(space, 3u, &item) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(space, 1u, inbox, 2u, &send_inbox) ==
           IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(space, 3u, item, 2u, &send_item) ==
           IOS27_MACH_OK);
    complex(packet, send_inbox, send_item);
    assert(ios27_mach_v2_send(space, 2u, packet, 40u, 0u) ==
           IOS27_MACH_OK);
    assert(ios27_mach_v2_task_teardown(space, 1u) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_rights(space, 2u, send_inbox, &a, &b, &receive) ==
           IOS27_MACH_OK && a == 1u);
    assert(ios27_mach_v2_receive(space, 1u, inbox, out, &size, 0u) ==
           IOS27_MACH_OK);
    assert(ios27_mach_v2_task_teardown(space, 1u) == IOS27_MACH_OK);
    assert(ios27_mach_v2_rights(space, 1u, rd32(out + 28u),
                                &a, &b, &receive) == IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_rights(space, 2u, send_inbox,
                                &a, &b, &receive) == IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_destroy(space, 1u, inbox) == IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_destroy(space, 3u, item) == IOS27_MACH_OK);
    uint32_t fresh = 0u;
    assert(ios27_mach_v2_allocate(space, 1u, &fresh) == IOS27_MACH_OK);
    assert(fresh != inbox);
    ios27_mach_v2_free(space);
}
static void teardown_queued_reply(void) {
    ios27_mach_v2 *space = ios27_mach_v2_new();
    uint32_t reply = 0u, service = 0u, service_alias = 0u;
    uint8_t packet[IOS27_MACH_MSG_MAX], out[IOS27_MACH_MSG_MAX];
    uint32_t once, a, b;
    unsigned r;
    size_t size = sizeof(out);
    assert(space);
    assert(ios27_mach_v2_allocate(space, 1u, &reply) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(space, 3u, &service) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(space, 3u, service, 1u, &service_alias) ==
           IOS27_MACH_OK);
    message(packet, service_alias,
            IOS27_MACH_COPY_SEND | (IOS27_MACH_MAKE_SEND_ONCE << 8u),
            reply, 7u);
    assert(ios27_mach_v2_send(space, 1u, packet, 24u, 0u) ==
           IOS27_MACH_OK);
    assert(ios27_mach_v2_task_teardown(space, 1u) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_receive(space, 3u, service, out, &size, 0u) ==
           IOS27_MACH_OK);
    once = rd32(out + 8u);
    assert(ios27_mach_v2_rights(space, 3u, once, &a, &b, &r) ==
           IOS27_MACH_OK && b == 1u);
    assert(ios27_mach_v2_task_teardown(space, 1u) ==
           IOS27_MACH_OK);
    assert(ios27_mach_v2_rights(space, 3u, once, &a, &b, &r) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_destroy(space, 3u, service) == IOS27_MACH_OK);
    ios27_mach_v2_free(space);
}
static void teardown_drops_messages(void) {
    ios27_mach_v2 *space = ios27_mach_v2_new();
    uint8_t packet[24u];
    uint32_t inbox, send_name;
    assert(space);
    assert(ios27_mach_v2_allocate(space, 1u, &inbox) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(space, 1u, inbox, 2u,
                                   &send_name) == IOS27_MACH_OK);
    message(packet, send_name, IOS27_MACH_COPY_SEND, 0u, 5u);
    assert(ios27_mach_v2_send(space, 2u, packet, 24u, 0u) == IOS27_MACH_OK);
    assert(ios27_mach_v2_task_teardown(space, 1u) == IOS27_MACH_OK);
    assert(ios27_mach_v2_send(space, 2u, packet, 24u, 0u) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_task_teardown(space, 9u) ==
           IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_v2_task_teardown(space, 1u) == IOS27_MACH_OK);
    ios27_mach_v2_free(space);
}
static void fixture_abi(void) {
    ios27_mach_v2 *space = ios27_mach_v2_new();
    ios27_darwin_guest_registers in = {{0}}, out = {{0}}, previous;
    _Alignas(8) uint8_t arena[512], snapshot[512];
    uint32_t service, reply_name, send_name, once;
    assert(space);
    memset(arena, 0xb4, sizeof(arena));
    assert(ios27_darwin_fixture_call(space, 1u,
            IOS27_DARWIN_FIXTURE_ALLOCATE, &in, &out,
            NULL, 0u) == IOS27_MACH_OK);
    service = (uint32_t)out.x[0];
    assert(ios27_darwin_fixture_call(space, 2u,
            IOS27_DARWIN_FIXTURE_ALLOCATE, &in, &out,
            NULL, 0u) == IOS27_MACH_OK);
    reply_name = (uint32_t)out.x[0];
    in.x[0] = service;
    in.x[1] = 2u;
    assert(ios27_darwin_fixture_call(space, 1u,
            IOS27_DARWIN_FIXTURE_MAKE_SEND, &in, &out,
            NULL, 0u) == IOS27_MACH_OK);
    send_name = (uint32_t)out.x[0];
    in.x[0] = send_name;
    assert(ios27_darwin_fixture_call(space, 2u,
            IOS27_DARWIN_FIXTURE_RIGHTS, &in, &out,
            NULL, 0u) == IOS27_MACH_OK);
    assert(out.x[0] == 1u && out.x[1] == 0u && out.x[2] == 0u);

    message(arena + 64u, send_name,
            IOS27_MACH_COPY_SEND | (IOS27_MACH_MAKE_SEND_ONCE << 8u),
            reply_name, 501u);
    memcpy(snapshot, arena, sizeof(arena));
    memset(&in, 0, sizeof(in));
    in.x[0] = 64u;
    in.x[1] = 24u;
    assert(ios27_darwin_fixture_call(space, 2u,
            IOS27_DARWIN_FIXTURE_SEND, &in, &out,
            arena, sizeof(arena)) == IOS27_MACH_OK);
    assert(out.x[0] == 24u && !memcmp(arena, snapshot, sizeof(arena)));

    memset(&in, 0, sizeof(in));
    in.x[0] = service;
    in.x[1] = 128u;
    in.x[2] = 0u;
    assert(ios27_darwin_fixture_call(space, 1u,
            IOS27_DARWIN_FIXTURE_RECEIVE, &in, &out,
            arena, sizeof(arena)) == IOS27_MACH_TOO_LARGE);
    assert(out.x[0] == 24u && !memcmp(arena, snapshot, sizeof(arena)));
    in.x[2] = 24u;
    assert(ios27_darwin_fixture_call(space, 1u,
            IOS27_DARWIN_FIXTURE_RECEIVE, &in, &out,
            arena, sizeof(arena)) == IOS27_MACH_OK);
    once = rd32(arena + 128u + 8u);
    assert(rd32(arena + 128u + 20u) == 501u && once != service);
    message(arena + 256u, once, IOS27_MACH_MOVE_SEND_ONCE, 0u, 502u);
    memset(&in, 0, sizeof(in));
    in.x[0] = 256u;
    in.x[1] = 24u;
    assert(ios27_darwin_fixture_call(space, 1u,
            IOS27_DARWIN_FIXTURE_SEND, &in, &out,
            arena, sizeof(arena)) == IOS27_MACH_OK);
    previous = out;
    assert(ios27_darwin_fixture_call(space, 1u,
            IOS27_DARWIN_FIXTURE_SEND, &in, &out,
            arena, sizeof(arena)) == IOS27_MACH_BAD_RIGHT);
    assert(!memcmp(&previous, &out, sizeof(out)));

    memset(&in, 0, sizeof(in));
    in.x[0] = reply_name;
    in.x[1] = 320u;
    in.x[2] = 24u;
    assert(ios27_darwin_fixture_call(space, 2u,
            IOS27_DARWIN_FIXTURE_RECEIVE, &in, &out,
            arena, sizeof(arena)) == IOS27_MACH_OK);
    assert(rd32(arena + 320u + 20u) == 502u);

    previous = out;
    assert(ios27_darwin_fixture_call(space, 2u, UINT32_C(0x1000000),
            &in, &out, arena, sizeof(arena)) == IOS27_MACH_UNSUPPORTED);
    assert(!memcmp(&previous, &out, sizeof(out)));
    in.x[0] = UINT64_MAX;
    assert(ios27_darwin_fixture_call(space, 2u,
            IOS27_DARWIN_FIXTURE_DESTROY, &in, &out,
            arena, sizeof(arena)) == IOS27_MACH_BAD_ARGUMENT);
    assert(!memcmp(&previous, &out, sizeof(out)));
    assert(ios27_darwin_fixture_call(space, 2u,
            IOS27_DARWIN_FIXTURE_DESTROY, &in, &in,
            arena, sizeof(arena)) == IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_darwin_fixture_call(space, 2u,
            IOS27_DARWIN_FIXTURE_DESTROY, &in,
            (ios27_darwin_guest_registers *)(void *)arena,
            arena, sizeof(arena)) == IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_darwin_fixture_call(space, 0u,
            IOS27_DARWIN_FIXTURE_SEND, &in, &out,
            arena, sizeof(arena)) == IOS27_MACH_BAD_ARGUMENT);

    assert(ios27_darwin_fixture_call(space, 1u,
            IOS27_DARWIN_FIXTURE_TEARDOWN, &in, &out,
            NULL, 0u) == IOS27_MACH_OK);
    assert(ios27_darwin_fixture_call(space, 2u,
            IOS27_DARWIN_FIXTURE_TEARDOWN, &in, &out,
            NULL, 0u) == IOS27_MACH_OK);
    ios27_mach_v2_free(space);
}

/* Phase 16C: guest-register ABI relay of an existing once right, without
 * revealing native pointers or losing it on rejected arguments. */
static void forwarded_reply_via_darwin_fixture(void) {
    ios27_mach_v2 *space = ios27_mach_v2_new();
    ios27_darwin_guest_registers in = {{0}}, out = {{0}}, previous;
    _Alignas(8) uint8_t arena[512], snapshot[512];
    uint32_t service, relay, reply, to_service, to_relay, once, forwarded;
    assert(space);
    assert(ios27_mach_v2_allocate(space, 1u, &service) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(space, 2u, &relay) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(space, 3u, &reply) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(space, 1u, service, 3u,
                                   &to_service) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(space, 2u, relay, 1u,
                                   &to_relay) == IOS27_MACH_OK);
    memset(arena, 0xa7, sizeof(arena));
    message(arena + 16u, to_service, IOS27_MACH_COPY_SEND |
            (IOS27_MACH_MAKE_SEND_ONCE << 8u), reply, 700u);
    memcpy(snapshot, arena, sizeof(arena));
    in.x[0] = 16u;
    in.x[1] = 24u;
    assert(ios27_darwin_fixture_call(space, 3u,
            IOS27_DARWIN_FIXTURE_SEND, &in, &out,
            arena, sizeof(arena)) == IOS27_MACH_OK);
    assert(out.x[0] == 24u && !memcmp(arena, snapshot, sizeof(arena)));
    memset(&in, 0, sizeof(in));
    in.x[0] = service;
    in.x[1] = 64u;
    in.x[2] = 24u;
    assert(ios27_darwin_fixture_call(space, 1u,
            IOS27_DARWIN_FIXTURE_RECEIVE, &in, &out,
            arena, sizeof(arena)) == IOS27_MACH_OK);
    once = rd32(arena + 64u + 8u);
    assert(rd32(arena + 64u + 20u) == 700u && once != 0u);

    message(arena + 128u, to_relay, IOS27_MACH_COPY_SEND |
            (IOS27_MACH_MOVE_SEND_ONCE << 8u), once, 701u);
    memset(&in, 0, sizeof(in));
    in.x[0] = 128u;
    in.x[1] = 24u;
    in.x[2] = UINT64_MAX;
    previous = out;
    memcpy(snapshot, arena, sizeof(arena));
    assert(ios27_darwin_fixture_call(space, 1u,
            IOS27_DARWIN_FIXTURE_SEND, &in, &out,
            arena, sizeof(arena)) == IOS27_MACH_BAD_ARGUMENT);
    assert(!memcmp(&out, &previous, sizeof(out)) &&
           !memcmp(arena, snapshot, sizeof(arena)));
    in.x[2] = 0u;
    assert(ios27_darwin_fixture_call(space, 1u,
            IOS27_DARWIN_FIXTURE_SEND, &in, &out,
            arena, sizeof(arena)) == IOS27_MACH_OK);
    assert(ios27_mach_v2_drop_send_once(space, 1u, once) ==
           IOS27_MACH_BAD_RIGHT);

    memset(&in, 0, sizeof(in));
    in.x[0] = relay;
    in.x[1] = 192u;
    in.x[2] = 24u;
    assert(ios27_darwin_fixture_call(space, 2u,
            IOS27_DARWIN_FIXTURE_RECEIVE, &in, &out,
            arena, sizeof(arena)) == IOS27_MACH_OK);
    forwarded = rd32(arena + 192u + 8u);
    assert(rd32(arena + 192u + 20u) == 701u && forwarded != 0u);
    message(arena + 256u, forwarded, IOS27_MACH_MOVE_SEND_ONCE,
            0u, 702u);
    memset(&in, 0, sizeof(in));
    in.x[0] = 256u;
    in.x[1] = 24u;
    assert(ios27_darwin_fixture_call(space, 2u,
            IOS27_DARWIN_FIXTURE_SEND, &in, &out,
            arena, sizeof(arena)) == IOS27_MACH_OK);
    memset(&in, 0, sizeof(in));
    in.x[0] = reply;
    in.x[1] = 320u;
    in.x[2] = 24u;
    assert(ios27_darwin_fixture_call(space, 3u,
            IOS27_DARWIN_FIXTURE_RECEIVE, &in, &out,
            arena, sizeof(arena)) == IOS27_MACH_OK);
    assert(rd32(arena + 320u + 20u) == 702u);
    ios27_mach_v2_free(space);
}

static void reject_unaligned_darwin_regs(void) {
    ios27_mach_v2 *space = ios27_mach_v2_new();
    _Alignas(8) uint8_t buffer[sizeof(ios27_darwin_guest_registers) + 8u];
    ios27_darwin_guest_registers in = {{0}}, out = {{0}};
    assert(space);
    memset(buffer, 0x4f, sizeof(buffer));
    assert(ios27_darwin_fixture_call(space, 1u,
            IOS27_DARWIN_FIXTURE_ALLOCATE,
            (ios27_darwin_guest_registers *)(void *)(buffer + 1u), &out,
            NULL, 0u) == IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_darwin_fixture_call(space, 1u,
            IOS27_DARWIN_FIXTURE_ALLOCATE, &in,
            (ios27_darwin_guest_registers *)(void *)(buffer + 1u),
            NULL, 0u) == IOS27_MACH_BAD_ARGUMENT);
    for (size_t i = 0u; i < sizeof(buffer); ++i)
        assert(buffer[i] == 0x4fu);
    ios27_mach_v2_free(space);
}

/* Exercise the same descriptor translation through the bounded guest arena
 * and AArch64 register facade, not only through the raw Mach fixture. */
static void abi_complex_descriptor_round_trip(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    ios27_darwin_guest_registers in = {{0}}, out = {{0}};
    uint8_t arena[256], before[256];
    uint32_t server, item, remote, item_alias, transferred;
    uint32_t sends, once;
    unsigned receive;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &server) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, 3u, &item) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, server, 2u, &remote) ==
           IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 3u, item, 2u, &item_alias) ==
           IOS27_MACH_OK);
    memset(arena, 0xa8, sizeof(arena));
    complex(arena + 32u, remote, item_alias);
    arena[32u + 38u] = IOS27_MACH_COPY_SEND;
    in.x[0] = 32u;
    in.x[1] = 40u;
    assert(ios27_darwin_fixture_call(s, 2u, IOS27_DARWIN_FIXTURE_SEND,
                                     &in, &out, arena, sizeof(arena)) ==
           IOS27_MACH_OK);
    assert(out.x[0] == 40u);
    assert(ios27_mach_v2_rights(s, 2u, item_alias, &sends, &once,
                                &receive) == IOS27_MACH_OK && sends == 1u);
    memcpy(before, arena, sizeof(arena));
    memset(&in, 0, sizeof(in));
    in.x[0] = server;
    in.x[1] = 128u;
    in.x[2] = 39u;
    assert(ios27_darwin_fixture_call(s, 1u, IOS27_DARWIN_FIXTURE_RECEIVE,
                                     &in, &out, arena, sizeof(arena)) ==
           IOS27_MACH_TOO_LARGE);
    assert(out.x[0] == 40u && !memcmp(arena, before, sizeof(arena)));
    in.x[2] = 40u;
    assert(ios27_darwin_fixture_call(s, 1u, IOS27_DARWIN_FIXTURE_RECEIVE,
                                     &in, &out, arena, sizeof(arena)) ==
           IOS27_MACH_OK);
    assert(out.x[0] == 40u);
    assert(arena[128u + 38u] == IOS27_MACH_MOVE_SEND);
    transferred = rd32(arena + 128u + 28u);
    assert(ios27_mach_v2_rights(s, 1u, transferred, &sends, &once,
                                &receive) == IOS27_MACH_OK && sends == 1u);
    ios27_mach_v2_free(s);
}


/* An ABI caller never receives a notification unless explicitly subscribed,
 * and failed polls leave the output register file untouched. */
static void phase17c_dead_name_fixture_dispatch(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    ios27_darwin_guest_registers in = {{0}}, out = {{0}};
    uint32_t owner, alias, other;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &owner) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, owner, 2u, &alias) ==
           IOS27_MACH_OK);
    out.x[0] = 0xabcd1234u;
    assert(ios27_darwin_fixture_call(s, 2u, IOS27_DARWIN_FIXTURE_POLL_DEAD,
                                     &in, &out, NULL, 0u) ==
           IOS27_MACH_TIMEOUT);
    assert(out.x[0] == 0xabcd1234u);
    in.x[0] = UINT64_MAX;
    assert(ios27_darwin_fixture_call(s, 2u, IOS27_DARWIN_FIXTURE_WATCH_DEAD,
                                     &in, &out, NULL, 0u) ==
           IOS27_MACH_BAD_ARGUMENT);
    in.x[0] = alias;
    assert(ios27_darwin_fixture_call(s, 2u, IOS27_DARWIN_FIXTURE_WATCH_DEAD,
                                     &in, &out, NULL, 0u) == IOS27_MACH_OK);
    assert(ios27_mach_v2_destroy(s, 1u, owner) == IOS27_MACH_OK);
    in.x[0] = 0u;
    assert(ios27_darwin_fixture_call(s, 2u, IOS27_DARWIN_FIXTURE_POLL_DEAD,
                                     &in, &out, NULL, 0u) == IOS27_MACH_OK);
    assert(out.x[0] == alias);
    out.x[0] = 991u;
    assert(ios27_darwin_fixture_call(s, 2u, IOS27_DARWIN_FIXTURE_POLL_DEAD,
                                     &in, &out, NULL, 0u) ==
           IOS27_MACH_TIMEOUT && out.x[0] == 991u);
    assert(ios27_mach_v2_allocate(s, 1u, &other) == IOS27_MACH_OK);
    assert(other != owner);
    ios27_mach_v2_free(s);
}


static void phase18c_abi_port_set_dispatch(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    ios27_darwin_guest_registers in = {{0}}, out = {{0}};
    uint8_t arena[512] = {0};
    uint32_t port, set, sender;
    assert(s);
    assert(ios27_darwin_fixture_call(s, 1u, IOS27_DARWIN_FIXTURE_ALLOCATE,
                                     &in, &out, NULL, 0u) == IOS27_MACH_OK);
    port = (uint32_t)out.x[0];
    assert(ios27_darwin_fixture_call(s, 1u,
                                     IOS27_DARWIN_FIXTURE_SET_ALLOCATE,
                                     &in, &out, NULL, 0u) == IOS27_MACH_OK);
    set = (uint32_t)out.x[0];
    in.x[0] = set;
    in.x[1] = port;
    assert(ios27_darwin_fixture_call(s, 2u, IOS27_DARWIN_FIXTURE_SET_ADD,
                                     &in, &out, NULL, 0u) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_darwin_fixture_call(s, 1u, IOS27_DARWIN_FIXTURE_SET_ADD,
                                     &in, &out, NULL, 0u) == IOS27_MACH_OK);
    in.x[0] = port;
    in.x[1] = 2u;
    assert(ios27_darwin_fixture_call(s, 1u, IOS27_DARWIN_FIXTURE_MAKE_SEND,
                                     &in, &out, NULL, 0u) == IOS27_MACH_OK);
    sender = (uint32_t)out.x[0];
    message(arena + 32u, sender, IOS27_MACH_COPY_SEND, 0u, 1871u);
    in.x[0] = 32u;
    in.x[1] = 24u;
    in.x[2] = 0u;
    assert(ios27_darwin_fixture_call(s, 2u, IOS27_DARWIN_FIXTURE_SEND,
                                     &in, &out, arena, sizeof(arena)) ==
           IOS27_MACH_OK);
    in.x[0] = set;
    in.x[1] = 128u;
    in.x[2] = 24u;
    in.x[3] = 0u;
    assert(ios27_darwin_fixture_call(s, 1u,
                                     IOS27_DARWIN_FIXTURE_RECEIVE_SET,
                                     &in, &out, arena, sizeof(arena)) ==
           IOS27_MACH_OK);
    assert(out.x[0] == 24u && rd32(arena + 148u) == 1871u);
    message(arena + 32u, sender, IOS27_MACH_COPY_SEND, 0u, 1872u);
    in.x[0] = 32u;
    in.x[1] = 24u;
    in.x[2] = 0u;
    assert(ios27_darwin_fixture_call(s, 2u, IOS27_DARWIN_FIXTURE_SEND,
                                     &in, &out, arena, sizeof(arena)) ==
           IOS27_MACH_OK);
    in.x[0] = port;
    in.x[1] = 128u;
    in.x[2] = 35u;
    in.x[3] = 0u;
    assert(ios27_darwin_fixture_call(s, 1u,
                                     IOS27_DARWIN_FIXTURE_RECEIVE_TRAILER,
                                     &in, &out, arena, sizeof(arena)) ==
           IOS27_MACH_TOO_LARGE && out.x[0] == 36u);
    in.x[2] = 36u;
    assert(ios27_darwin_fixture_call(s, 1u,
                                     IOS27_DARWIN_FIXTURE_RECEIVE_TRAILER,
                                     &in, &out, arena, sizeof(arena)) ==
           IOS27_MACH_OK && out.x[0] == 36u);
    assert(rd32(arena + 148u) == 1872u);
    assert(rd32(arena + 156u) == 12u && rd32(arena + 160u) == 1u);
    in.x[0] = set;
    in.x[1] = port;
    assert(ios27_darwin_fixture_call(s, 1u,
                                     IOS27_DARWIN_FIXTURE_SET_REMOVE,
                                     &in, &out, NULL, 0u) == IOS27_MACH_OK);
    in.x[0] = set;
    assert(ios27_darwin_fixture_call(s, 1u,
                                     IOS27_DARWIN_FIXTURE_SET_DESTROY,
                                     &in, &out, NULL, 0u) == IOS27_MACH_OK);
    ios27_mach_v2_free(s);
}

static void phase18c_reject_abi_state_alias(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    ios27_darwin_guest_registers in = {{0}}, out = {{0}};
    assert(s);
    assert(ios27_darwin_fixture_call(s, 1u,
            IOS27_DARWIN_FIXTURE_ALLOCATE, &in,
            (ios27_darwin_guest_registers *)(void *)s,
            NULL, 0u) == IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_darwin_fixture_call(s, 1u,
            IOS27_DARWIN_FIXTURE_ALLOCATE, &in, &out,
            (uint8_t *)(void *)s, 64u) == IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_darwin_fixture_call(s, 1u,
            IOS27_DARWIN_FIXTURE_ALLOCATE, &in, &out,
            NULL, 0u) == IOS27_MACH_OK);
    ios27_mach_v2_free(s);
}


static void phase18c_abi_bounded_ool(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    ios27_darwin_guest_registers in = {{0}}, out = {{0}};
    uint8_t arena[512] = {0};
    uint32_t port, sender;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &port) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, port, 2u, &sender) ==
           IOS27_MACH_OK);
    message(arena + 32u, sender,
            IOS27_MACH_COMPLEX | IOS27_MACH_COPY_SEND, 0u, 1891u);
    wr32(arena + 36u, 44u);
    wr32(arena + 56u, 1u);
    wr32(arena + 60u, 300u);
    arena[71u] = IOS27_MACH_OOL_DESCRIPTOR;
    wr32(arena + 72u, 4u);
    arena[300u] = 0x11u;
    arena[301u] = 0x22u;
    arena[302u] = 0x33u;
    arena[303u] = 0x44u;
    in.x[0] = 32u;
    in.x[1] = 44u;
    in.x[2] = 0u;
    assert(ios27_darwin_fixture_call(s, 2u, IOS27_DARWIN_FIXTURE_SEND,
                                     &in, &out, arena, sizeof(arena)) ==
           IOS27_MACH_OK);
    in.x[0] = port;
    in.x[1] = 128u;
    in.x[2] = 47u;
    in.x[3] = 0u;
    assert(ios27_darwin_fixture_call(s, 1u,
                                     IOS27_DARWIN_FIXTURE_RECEIVE_OOL,
                                     &in, &out, arena, sizeof(arena)) ==
           IOS27_MACH_TOO_LARGE && out.x[0] == 48u);
    in.x[2] = 48u;
    assert(ios27_darwin_fixture_call(s, 1u,
                                     IOS27_DARWIN_FIXTURE_RECEIVE_OOL,
                                     &in, &out, arena, sizeof(arena)) ==
           IOS27_MACH_OK && out.x[0] == 48u);
    assert(rd32(arena + 148u) == 1891u);
    assert(rd32(arena + 156u) == 172u);
    assert(rd32(arena + 172u) == 0x44332211u);
    ios27_mach_v2_free(s);
}

static void phase19c_fixture_identity_exception_vm(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    ios27_darwin_guest_registers in = {{0}}, out = {{0}}, untouched;
    uint8_t arena[256], reference[256], snapshot[256];
    uint32_t receiver = 0u, sender = 0u, thread = 0u;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &receiver) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, receiver, 2u, &sender) ==
           IOS27_MACH_OK);
    assert(ios27_darwin_fixture_call(s, 2u,
           IOS27_DARWIN_FIXTURE_THREAD_CREATE,
           &in, &out, NULL, 0u) == IOS27_MACH_OK);
    thread = (uint32_t)out.x[0];
    in.x[0] = thread;
    assert(ios27_darwin_fixture_call(s, 2u,
           IOS27_DARWIN_FIXTURE_THREAD_IDENTITY,
           &in, &out, NULL, 0u) == IOS27_MACH_OK && out.x[0] == 2u);
    assert(ios27_darwin_fixture_call(s, 3u,
           IOS27_DARWIN_FIXTURE_THREAD_IDENTITY,
           &in, &out, NULL, 0u) == IOS27_MACH_BAD_RIGHT);
    in.x[0] = 3u;
    in.x[1] = sender;
    assert(ios27_darwin_fixture_call(s, 2u,
           IOS27_DARWIN_FIXTURE_EXCEPTION_SET,
           &in, &out, NULL, 0u) == IOS27_MACH_OK);
    in.x[0] = 1u;
    assert(ios27_darwin_fixture_call(s, 2u,
           IOS27_DARWIN_FIXTURE_EXCEPTION_GET,
           &in, &out, NULL, 0u) == IOS27_MACH_OK &&
           out.x[0] == sender);
    in.x[0] = thread;
    in.x[1] = 1u;
    in.x[2] = 991u;
    assert(ios27_darwin_fixture_call(s, 2u,
           IOS27_DARWIN_FIXTURE_EXCEPTION_RAISE,
           &in, &out, NULL, 0u) == IOS27_MACH_OK);
    memset(arena, 0xab, sizeof(arena));
    in.x[0] = receiver;
    in.x[1] = 32u;
    in.x[2] = 39u;
    in.x[3] = 0u;
    assert(ios27_darwin_fixture_call(s, 1u,
           IOS27_DARWIN_FIXTURE_RECEIVE,
           &in, &out, arena, sizeof(arena)) == IOS27_MACH_TOO_LARGE &&
           out.x[0] == 40u && arena[32u] == 0xabu);
    in.x[2] = 40u;
    assert(ios27_darwin_fixture_call(s, 1u,
           IOS27_DARWIN_FIXTURE_RECEIVE,
           &in, &out, arena, sizeof(arena)) == IOS27_MACH_OK &&
           out.x[0] == 40u);
    assert(rd32(arena + 52u) == IOS27_MACH_V2_EXCEPTION_MSG_ID);
    assert(rd32(arena + 56u) == thread && rd32(arena + 60u) == 2u);
    assert(rd32(arena + 64u) == 1u && rd32(arena + 68u) == 991u);
    in.x[0] = thread;
    assert(ios27_darwin_fixture_call(s, 2u,
           IOS27_DARWIN_FIXTURE_THREAD_DESTROY,
           &in, &out, NULL, 0u) == IOS27_MACH_OK);
    in.x[0] = thread;
    in.x[1] = 1u;
    in.x[2] = 992u;
    assert(ios27_darwin_fixture_call(s, 2u,
           IOS27_DARWIN_FIXTURE_EXCEPTION_RAISE,
           &in, &out, NULL, 0u) == IOS27_MACH_BAD_RIGHT);
    for (size_t i = 0u; i < sizeof(arena); ++i)
        arena[i] = (uint8_t)i;
    memcpy(reference, arena, sizeof(arena));
    memmove(reference + 16u, reference + 8u, 80u);
    in.x[0] = 8u;
    in.x[1] = 16u;
    in.x[2] = 80u;
    assert(ios27_darwin_fixture_call(s, 2u,
           IOS27_DARWIN_FIXTURE_VM_COPY,
           &in, &out, arena, sizeof(arena)) == IOS27_MACH_OK &&
           out.x[0] == 80u);
    assert(memcmp(reference, arena, sizeof(arena)) == 0);
    memset(reference + 48u, 0, 64u);
    in.x[0] = 48u;
    in.x[1] = 64u;
    assert(ios27_darwin_fixture_call(s, 2u,
           IOS27_DARWIN_FIXTURE_VM_ZERO,
           &in, &out, arena, sizeof(arena)) == IOS27_MACH_OK &&
           out.x[0] == 64u);
    assert(memcmp(reference, arena, sizeof(arena)) == 0);
    memcpy(snapshot, arena, sizeof(arena));
    memset(&out, 0x55, sizeof(out));
    untouched = out;
    in.x[0] = 250u;
    in.x[1] = 80u;
    in.x[2] = 10u;
    assert(ios27_darwin_fixture_call(s, 2u,
           IOS27_DARWIN_FIXTURE_VM_COPY,
           &in, &out, arena, sizeof(arena)) == IOS27_MACH_BAD_ARGUMENT);
    assert(memcmp(snapshot, arena, sizeof(arena)) == 0);
    assert(memcmp(&out, &untouched, sizeof(out)) == 0);
    in.x[0] = UINT64_MAX;
    in.x[1] = 1u;
    assert(ios27_darwin_fixture_call(s, 2u,
           IOS27_DARWIN_FIXTURE_VM_ZERO,
           &in, &out, arena, sizeof(arena)) == IOS27_MACH_BAD_ARGUMENT);
    assert(memcmp(snapshot, arena, sizeof(arena)) == 0);
    in.x[0] = 0u;
    in.x[1] = IOS27_DARWIN_FIXTURE_VM_MAX + 1u;
    assert(ios27_darwin_fixture_call(s, 2u,
           IOS27_DARWIN_FIXTURE_VM_ZERO,
           &in, &out, arena, sizeof(arena)) == IOS27_MACH_BAD_ARGUMENT);
    assert(memcmp(&out, &untouched, sizeof(out)) == 0);
    ios27_mach_v2_free(s);
}

int main(void) {
    phase19c_fixture_identity_exception_vm();
    phase18c_abi_bounded_ool();
    phase18c_reject_abi_state_alias();
    phase18c_abi_port_set_dispatch();
    phase17c_dead_name_fixture_dispatch();
    abi_complex_descriptor_round_trip();
    reject_unaligned_darwin_regs();
    forwarded_reply_via_darwin_fixture();
    teardown_inflight();
    teardown_queued_reply();
    teardown_drops_messages();
    fixture_abi();
    puts("phase11 task teardown and bounded Darwin register facade passed");
    return 0;
}
