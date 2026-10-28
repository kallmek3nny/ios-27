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

int main(void) {
    teardown_inflight();
    teardown_queued_reply();
    teardown_drops_messages();
    fixture_abi();
    abi_complex_descriptor_round_trip();
    puts("phase11 task teardown and bounded Darwin register facade passed");
    return 0;
}
