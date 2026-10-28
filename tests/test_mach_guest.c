#include "ios27/mach_guest.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void le32(uint8_t *p, uint32_t value) {
    for (unsigned i=0u; i<4u; ++i) p[i]=(uint8_t)(value>>(i*8u));
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static void wire(uint8_t *p, uint32_t remote, uint32_t bits,
                 uint32_t local, uint32_t id) {
    memset(p, 0, 24u);
    le32(p, bits);
    le32(p+4u, 24u);
    le32(p+8u, remote);
    le32(p+12u, local);
    le32(p+20u, id);
}
static void round_trip(void) {
    ios27_mach_v2 *s=ios27_mach_v2_new();
    uint8_t arena[512], snapshot[512];
    uint32_t srv=0u, cli=0u, send_name=0u, reply_name=0u;
    uint32_t actual=0u;
    assert(s);
    assert(ios27_mach_v2_allocate(s,1u,&srv)==IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s,2u,&cli)==IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s,1u,srv,2u,&send_name)==IOS27_MACH_OK);
    memset(arena,0xa5,sizeof(arena));
    wire(arena+64u,send_name,
         IOS27_MACH_COPY_SEND | (IOS27_MACH_MAKE_SEND_ONCE<<8u),
         cli,2027u);
    memcpy(snapshot,arena,sizeof(arena));
    assert(ios27_mach_guest_call(s,2u,IOS27_MACH_GUEST_SEND,0u,
                                 arena,sizeof(arena),64u,24u,0u,
                                 &actual)==IOS27_MACH_OK);
    assert(actual==24u && memcmp(arena,snapshot,sizeof(arena))==0);
    actual=0u;
    assert(ios27_mach_guest_call(s,1u,IOS27_MACH_GUEST_RECEIVE,srv,
                                 arena,sizeof(arena),128u,0u,0u,
                                 &actual)==IOS27_MACH_TOO_LARGE);
    assert(actual==24u && memcmp(arena,snapshot,sizeof(arena))==0);
    actual=0u;
    assert(ios27_mach_guest_call(s,1u,IOS27_MACH_GUEST_RECEIVE,srv,
                                 arena,sizeof(arena),128u,64u,0u,
                                 &actual)==IOS27_MACH_OK);
    assert(actual==24u && rd32(arena+128u+12u)==srv);
    assert(rd32(arena+128u+20u)==2027u);
    reply_name=rd32(arena+128u+8u);
    assert(reply_name!=0u && reply_name!=srv);
    wire(arena+320u,reply_name,IOS27_MACH_MOVE_SEND_ONCE,0u,42u);
    actual=0u;
    assert(ios27_mach_guest_call(s,1u,IOS27_MACH_GUEST_SEND,0u,
                                 arena,sizeof(arena),320u,24u,0u,
                                 &actual)==IOS27_MACH_OK);
    assert(actual==24u);
    actual=1234u;
    assert(ios27_mach_guest_call(s,1u,IOS27_MACH_GUEST_SEND,0u,
                                 arena,sizeof(arena),320u,24u,0u,
                                 &actual)==IOS27_MACH_BAD_RIGHT);
    assert(actual==1234u);
    actual=0u;
    assert(ios27_mach_guest_call(s,2u,IOS27_MACH_GUEST_RECEIVE,cli,
                                 arena,sizeof(arena),400u,64u,0u,
                                 &actual)==IOS27_MACH_OK);
    assert(actual==24u && rd32(arena+400u+20u)==42u);
    assert(rd32(arena+400u+12u)==cli);
    ios27_mach_v2_free(s);
}
static void invalid_arena_offsets(void) {
    ios27_mach_v2 *s=ios27_mach_v2_new();
    uint8_t arena[512], original[512];
    uint32_t p=0u, actual=77u;
    assert(s);
    assert(ios27_mach_v2_allocate(s,1u,&p)==IOS27_MACH_OK);
    memset(arena,0x66,sizeof(arena));
    memcpy(original,arena,sizeof(arena));
    assert(ios27_mach_guest_call(s,1u,IOS27_MACH_GUEST_SEND,0u,
                                 arena,sizeof(arena),UINT64_MAX,24u,
                                 0u,&actual)==IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_guest_call(s,1u,IOS27_MACH_GUEST_SEND,0u,
                                 arena,sizeof(arena),500u,24u,
                                 0u,&actual)==IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_guest_call(s,1u,IOS27_MACH_GUEST_SEND,0u,
                                 arena,sizeof(arena),0u,257u,
                                 0u,&actual)==IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_guest_call(s,1u,IOS27_MACH_GUEST_SEND,0u,
                                 arena,sizeof(arena),0u,20u,
                                 0u,&actual)==IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_guest_call(s,1u,999u,0u,
                                 arena,sizeof(arena),0u,24u,
                                 0u,&actual)==IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_guest_call(s,1u,IOS27_MACH_GUEST_RECEIVE,p,
                                 arena,sizeof(arena),512u,0u,
                                 0u,&actual)==IOS27_MACH_TIMEOUT);
    assert(ios27_mach_guest_call(s,1u,IOS27_MACH_GUEST_RECEIVE,99999u,
                                 arena,sizeof(arena),0u,24u,
                                 0u,&actual)==IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_guest_call(s,1u,IOS27_MACH_GUEST_RECEIVE,p,
                                 arena,sizeof(arena),0u,24u,
                                 0u,(uint32_t *)(void *)(arena+32u))==
           IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_guest_call(s,1u,IOS27_MACH_GUEST_SEND,0u,
                                 NULL,sizeof(arena),0u,24u,0u,
                                 &actual)==IOS27_MACH_BAD_ARGUMENT);
    assert(actual==77u);
    assert(memcmp(original,arena,sizeof(arena))==0);
    ios27_mach_v2_free(s);
}

/* Result placement is guest-virtual and never aliases a host result pointer. */
static void offset_result_round_trip(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint8_t arena[512], expected[512];
    uint32_t server, client, destination, reply;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &server) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, 2u, &client) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, server, 2u, &destination) ==
           IOS27_MACH_OK);
    memset(arena, 0xa5, sizeof(arena));
    wire(arena + 64u, destination,
         IOS27_MACH_COPY_SEND | (IOS27_MACH_MAKE_SEND_ONCE << 8u),
         client, 314u);
    memcpy(expected, arena, sizeof(arena));
    assert(ios27_mach_guest_call_offset(s, 2u, IOS27_MACH_GUEST_SEND, 0u,
                                        arena, sizeof(arena), 64u, 24u, 0u,
                                        32u) == IOS27_MACH_OK);
    le32(expected + 32u, 24u);
    assert(memcmp(arena, expected, sizeof(arena)) == 0);

    assert(ios27_mach_guest_call_offset(s, 1u, IOS27_MACH_GUEST_RECEIVE,
                                        server, arena, sizeof(arena), 160u,
                                        64u, 0u, 40u) == IOS27_MACH_OK);
    assert(rd32(arena + 40u) == 24u);
    assert(rd32(arena + 160u + 12u) == server);
    assert(rd32(arena + 160u + 20u) == 314u);
    reply = rd32(arena + 160u + 8u);
    assert(reply != 0u);
    wire(arena + 256u, reply, IOS27_MACH_MOVE_SEND_ONCE, 0u, 271u);
    assert(ios27_mach_guest_call_offset(s, 1u, IOS27_MACH_GUEST_SEND, 0u,
                                        arena, sizeof(arena), 256u, 24u, 0u,
                                        48u) == IOS27_MACH_OK);
    assert(rd32(arena + 48u) == 24u);
    memcpy(expected, arena, sizeof(arena));
    assert(ios27_mach_guest_call_offset(s, 1u, IOS27_MACH_GUEST_SEND, 0u,
                                        arena, sizeof(arena), 256u, 24u, 0u,
                                        48u) == IOS27_MACH_BAD_RIGHT);
    assert(memcmp(arena, expected, sizeof(arena)) == 0);
    assert(ios27_mach_guest_call_offset(s, 2u, IOS27_MACH_GUEST_RECEIVE,
                                        client, arena, sizeof(arena), 384u,
                                        64u, 0u, 52u) == IOS27_MACH_OK);
    assert(rd32(arena + 52u) == 24u);
    assert(rd32(arena + 384u + 20u) == 271u);
    ios27_mach_v2_free(s);
}

static void offset_result_rollback(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint8_t arena[512], before[512];
    uint32_t server, sender;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &server) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, server, 2u, &sender) ==
           IOS27_MACH_OK);
    memset(arena, 0x5a, sizeof(arena));
    wire(arena + 64u, sender, IOS27_MACH_COPY_SEND, 0u, 900u);
    memcpy(before, arena, sizeof(arena));
    /* Complete and partial overlap with the wire are rejected unchanged. */
    assert(ios27_mach_guest_call_offset(s, 2u, IOS27_MACH_GUEST_SEND, 0u,
                                        arena, sizeof(arena), 64u, 24u, 0u,
                                        84u) == IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_guest_call_offset(s, 2u, IOS27_MACH_GUEST_SEND, 0u,
                                        arena, sizeof(arena), 64u, 24u, 0u,
                                        64u) == IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_guest_call_offset(s, 2u, IOS27_MACH_GUEST_SEND, 0u,
                                        arena, sizeof(arena), 64u, 24u, 0u,
                                        510u) == IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_guest_call_offset(s, 2u, IOS27_MACH_GUEST_SEND, 0u,
                                        arena, sizeof(arena), 64u, 24u, 0u,
                                        UINT64_MAX) == IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_guest_call_offset(s, 2u, IOS27_MACH_GUEST_SEND, 0u,
                                        arena, sizeof(arena), UINT64_MAX, 24u,
                                        0u, 32u) == IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_guest_call_offset(s, 2u, 999u, 0u, arena,
                                        sizeof(arena), 64u, 24u, 0u,
                                        32u) == IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_guest_call_offset(s, 2u, IOS27_MACH_GUEST_RECEIVE,
                                        999u, arena, sizeof(arena), 160u,
                                        24u, 0u, 32u) == IOS27_MACH_BAD_RIGHT);
    assert(memcmp(arena, before, sizeof(arena)) == 0);

    assert(ios27_mach_guest_call_offset(s, 2u, IOS27_MACH_GUEST_SEND, 0u,
                                        arena, sizeof(arena), 64u, 24u, 0u,
                                        32u) == IOS27_MACH_OK);
    /* Too-small receive reports needed bytes but never consumes the queue. */
    memcpy(before, arena, sizeof(arena));
    assert(ios27_mach_guest_call_offset(s, 1u, IOS27_MACH_GUEST_RECEIVE,
                                        server, arena, sizeof(arena), 192u,
                                        0u, 0u, 40u) == IOS27_MACH_TOO_LARGE);
    le32(before + 40u, 24u);
    assert(memcmp(arena, before, sizeof(arena)) == 0);
    assert(ios27_mach_guest_call_offset(s, 1u, IOS27_MACH_GUEST_RECEIVE,
                                        server, arena, sizeof(arena), 192u,
                                        24u, 0u, 40u) == IOS27_MACH_OK);
    assert(rd32(arena + 192u + 20u) == 900u);
    for (unsigned i = 0u; i < IOS27_MACH_QUEUE_MAX; ++i) {
        wire(arena + 64u, sender, IOS27_MACH_COPY_SEND, 0u, i + 1u);
        assert(ios27_mach_guest_call_offset(s, 2u, IOS27_MACH_GUEST_SEND,
                                            0u, arena, sizeof(arena), 64u,
                                            24u, 0u, 32u) == IOS27_MACH_OK);
    }
    wire(arena + 64u, sender, IOS27_MACH_MOVE_SEND, 0u, 999u);
    memcpy(before, arena, sizeof(arena));
    assert(ios27_mach_guest_call_offset(s, 2u, IOS27_MACH_GUEST_SEND, 0u,
                                        arena, sizeof(arena), 64u, 24u, 0u,
                                        32u) == IOS27_MACH_TIMEOUT);
    assert(memcmp(arena, before, sizeof(arena)) == 0);
    for (unsigned i = 0u; i < IOS27_MACH_QUEUE_MAX; ++i) {
        assert(ios27_mach_guest_call_offset(s, 1u, IOS27_MACH_GUEST_RECEIVE,
                                            server, arena, sizeof(arena),
                                            192u, 24u, 0u, 40u) ==
               IOS27_MACH_OK);
        assert(rd32(arena + 192u + 20u) == i + 1u);
    }
    ios27_mach_v2_free(s);
}

static void reject_unaligned_host_result(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    _Alignas(8) uint8_t arena[64];
    uint32_t port;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &port) == IOS27_MACH_OK);
    memset(arena, 0x7a, sizeof(arena));
    assert(ios27_mach_guest_call(s, 1u, IOS27_MACH_GUEST_RECEIVE,
                                 port, arena, 32u, 0u, 24u, 0u,
                                 (uint32_t *)(void *)(arena + 33u)) ==
           IOS27_MACH_BAD_ARGUMENT);
    for (size_t i = 0u; i < sizeof(arena); ++i)
        assert(arena[i] == 0x7au);
    ios27_mach_v2_free(s);
}


/* Real wire-length validation across the guest-virtual offset facade:
 * multiple descriptors must survive TOO_LARGE, collision and normalization
 * without a partial guest store or a premature dequeue. */
static void phase17c_complex_guest_offsets(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t server, item, dest, alias, sends, once;
    unsigned receive;
    uint8_t arena[512], before[512];
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &server) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, 3u, &item) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, server, 2u, &dest) ==
           IOS27_MACH_OK);
    for (unsigned i = 0u; i < 2u; ++i)
        assert(ios27_mach_v2_make_send(s, 3u, item, 2u, &alias) ==
               IOS27_MACH_OK);
    memset(arena, 0, sizeof(arena));
    wire(arena + 64u, dest, IOS27_MACH_COMPLEX | IOS27_MACH_COPY_SEND,
         0u, 1710u);
    le32(arena + 68u, 52u);
    le32(arena + 88u, 2u);
    for (unsigned i = 0u; i < 2u; ++i) {
        size_t off = 64u + 28u + i * 12u;
        le32(arena + off, alias);
        arena[off + 10u] = IOS27_MACH_MOVE_SEND;
        arena[off + 11u] = IOS27_MACH_PORT_DESCRIPTOR;
    }
    assert(ios27_mach_guest_call_offset(s, 2u, IOS27_MACH_GUEST_SEND, 0u,
                                        arena, sizeof(arena), 64u, 52u,
                                        0u, 32u) == IOS27_MACH_OK);
    assert(rd32(arena + 32u) == 52u);
    assert(ios27_mach_v2_rights(s, 2u, alias, &sends, &once, &receive) ==
           IOS27_MACH_BAD_RIGHT);
    memcpy(before, arena, sizeof(arena));
    assert(ios27_mach_guest_call_offset(s, 1u, IOS27_MACH_GUEST_RECEIVE,
                                        server, arena, sizeof(arena), 128u,
                                        51u, 0u, 40u) == IOS27_MACH_TOO_LARGE);
    assert(rd32(arena + 40u) == 52u);
    assert(memcmp(arena + 128u, before + 128u, 52u) == 0);
    memcpy(before, arena, sizeof(arena));
    assert(ios27_mach_guest_call_offset(s, 1u, IOS27_MACH_GUEST_RECEIVE,
                                        server, arena, sizeof(arena), 128u,
                                        52u, 0u, 140u) == IOS27_MACH_BAD_ARGUMENT);
    assert(memcmp(arena, before, sizeof(arena)) == 0);
    assert(ios27_mach_guest_call_offset(s, 1u, IOS27_MACH_GUEST_RECEIVE,
                                        server, arena, sizeof(arena), 128u,
                                        52u, 0u, 40u) == IOS27_MACH_OK);
    assert(rd32(arena + 40u) == 52u);
    assert(rd32(arena + 128u + 24u) == 2u);
    assert(arena[128u + 38u] == IOS27_MACH_MOVE_SEND);
    assert(arena[128u + 50u] == IOS27_MACH_MOVE_SEND);
    uint32_t received = rd32(arena + 128u + 28u);
    assert(rd32(arena + 128u + 40u) == received);
    assert(ios27_mach_v2_rights(s, 1u, received, &sends, &once, &receive) ==
           IOS27_MACH_OK && sends == 2u);
    ios27_mach_v2_free(s);
}


/* Guest-virtual selector paths must never expose host pointers, and a
 * short trailer receive must write only its explicit result slot. */
static void phase18c_guest_portset_and_trailer(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint32_t port, send, set;
    uint8_t arena[512], saved[512];
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &port) == IOS27_MACH_OK);
    assert(ios27_mach_v2_set_allocate(s, 1u, &set) == IOS27_MACH_OK);
    assert(ios27_mach_v2_set_add(s, 1u, set, port) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, port, 2u, &send) ==
           IOS27_MACH_OK);
    memset(arena, 0xcd, sizeof(arena));
    wire(arena + 64u, send, IOS27_MACH_COPY_SEND, 0u, 1801u);
    assert(ios27_mach_guest_call_offset(s, 2u, IOS27_MACH_GUEST_SEND,
                                        0u, arena, sizeof(arena), 64u,
                                        24u, 0u, 32u) == IOS27_MACH_OK);
    assert(ios27_mach_guest_call_offset(s, 1u,
                                        IOS27_MACH_GUEST_RECEIVE_SET,
                                        set, arena, sizeof(arena), 128u,
                                        23u, 0u, 40u) == IOS27_MACH_TOO_LARGE);
    assert(rd32(arena + 40u) == 24u);
    assert(ios27_mach_guest_call_offset(s, 1u,
                                        IOS27_MACH_GUEST_RECEIVE_SET,
                                        set, arena, sizeof(arena), 128u,
                                        24u, 0u, 40u) == IOS27_MACH_OK);
    assert(rd32(arena + 148u) == 1801u);
    wire(arena + 64u, send, IOS27_MACH_COPY_SEND, 0u, 1802u);
    assert(ios27_mach_guest_call_offset(s, 2u, IOS27_MACH_GUEST_SEND,
                                        0u, arena, sizeof(arena), 64u,
                                        24u, 0u, 32u) == IOS27_MACH_OK);
    memcpy(saved, arena, sizeof(arena));
    assert(ios27_mach_guest_call_offset(s, 1u,
                                        IOS27_MACH_GUEST_RECEIVE_TRAILER,
                                        port, arena, sizeof(arena), 192u,
                                        35u, 0u, 40u) == IOS27_MACH_TOO_LARGE);
    assert(rd32(arena + 40u) == 36u);
    assert(memcmp(arena + 192u, saved + 192u, 36u) == 0);
    assert(ios27_mach_guest_call_offset(s, 1u,
                                        IOS27_MACH_GUEST_RECEIVE_TRAILER,
                                        port, arena, sizeof(arena), 192u,
                                        36u, 0u, 40u) == IOS27_MACH_OK);
    assert(rd32(arena + 212u) == 1802u);
    assert(rd32(arena + 220u) == 12u);
    assert(rd32(arena + 224u) == 1u);
    memcpy(saved, arena, sizeof(arena));
    assert(ios27_mach_guest_call_offset(s, 1u,
                                        IOS27_MACH_GUEST_RECEIVE_TRAILER,
                                        port, arena, sizeof(arena), 192u,
                                        36u, 0u, 200u) ==
           IOS27_MACH_BAD_ARGUMENT);
    assert(memcmp(arena, saved, sizeof(arena)) == 0);
    ios27_mach_v2_free(s);
}

static void phase18c_guest_rejects_host_alias(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint8_t arena[64] = {0};
    uint32_t actual = 123u, port = 0u;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &port) == IOS27_MACH_OK);
    assert(ios27_mach_guest_call(s, 1u, IOS27_MACH_GUEST_RECEIVE,
                                 port, (uint8_t *)(void *)s, 24u,
                                 0u, 24u, 0u, &actual) ==
           IOS27_MACH_BAD_ARGUMENT);
    assert(actual == 123u);
    assert(ios27_mach_guest_call(s, 1u, IOS27_MACH_GUEST_RECEIVE,
                                 port, arena, sizeof(arena),
                                 0u, 24u, 0u, (uint32_t *)(void *)s) ==
           IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_guest_call(s, 1u, IOS27_MACH_GUEST_RECEIVE,
                                 port, arena, sizeof(arena), 0u,
                                 24u, 0u, &actual) == IOS27_MACH_TIMEOUT);
    ios27_mach_v2_free(s);
}


/* A mixed port-right + Darwin LP64 OOL-memory descriptor transfers both
 * atomically. OOL offsets are guest-virtual, never host dereferences. */
static void phase18c_bounded_ool_mixed_transfer(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint8_t arena[512] = {0}, before[512];
    uint32_t inbox, item, dest, alias, sends, once;
    unsigned receive;
    uint32_t delivered;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &inbox) == IOS27_MACH_OK);
    assert(ios27_mach_v2_allocate(s, 3u, &item) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, inbox, 2u, &dest) ==
           IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 3u, item, 2u, &alias) ==
           IOS27_MACH_OK);
    wire(arena + 32u, dest, IOS27_MACH_COMPLEX | IOS27_MACH_COPY_SEND,
         0u, 1881u);
    le32(arena + 36u, 56u);
    le32(arena + 56u, 2u);
    le32(arena + 60u, alias); /* port descriptor begins at wire +28 */
    arena[70u] = IOS27_MACH_MOVE_SEND;
    arena[71u] = IOS27_MACH_PORT_DESCRIPTOR;
    le32(arena + 72u, 320u); /* OOL guest offset, wire +40 */
    arena[81u] = 1u; /* MACH_MSG_VIRTUAL_COPY */
    arena[83u] = IOS27_MACH_OOL_DESCRIPTOR;
    le32(arena + 84u, 48u);
    for (size_t i = 0u; i < 48u; ++i)
        arena[320u + i] = (uint8_t)(0x41u + i);
    memcpy(before, arena, sizeof(arena));
    assert(ios27_mach_guest_call_offset(s, 2u, IOS27_MACH_GUEST_SEND,
                                        0u, arena, sizeof(arena), 32u,
                                        56u, 0u, 24u) == IOS27_MACH_OK);
    assert(ios27_mach_v2_rights(s, 2u, alias, &sends, &once, &receive) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_v2_destroy(s, 3u, item) == IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_guest_call_offset(s, 1u,
                                        IOS27_MACH_GUEST_RECEIVE,
                                        inbox, arena, sizeof(arena), 128u,
                                        IOS27_MACH_MSG_MAX + 1u, 0u, 24u) ==
           IOS27_MACH_BAD_ARGUMENT);
    /* Ordinary receive has at most a 256-byte wire capacity and is
     * deliberately unable to dequeue a message with OOL payload. */
    assert(ios27_mach_guest_call_offset(s, 1u,
                                        IOS27_MACH_GUEST_RECEIVE,
                                        inbox, arena, sizeof(arena), 128u,
                                        56u, 0u, 24u) ==
           IOS27_MACH_UNSUPPORTED);
    memcpy(before, arena, sizeof(arena));
    assert(ios27_mach_guest_call_offset(s, 1u,
                                        IOS27_MACH_GUEST_RECEIVE_OOL,
                                        inbox, arena, sizeof(arena), 128u,
                                        103u, 0u, 24u) ==
           IOS27_MACH_TOO_LARGE);
    assert(rd32(arena + 24u) == 104u);
    assert(memcmp(arena + 128u, before + 128u, 104u) == 0);
    assert(ios27_mach_guest_call_offset(s, 1u,
                                        IOS27_MACH_GUEST_RECEIVE_OOL,
                                        inbox, arena, sizeof(arena), 128u,
                                        104u, 0u, 24u) == IOS27_MACH_OK);
    assert(rd32(arena + 24u) == 104u);
    assert(rd32(arena + 132u) == 56u);
    assert(rd32(arena + 148u) == 1881u);
    delivered = rd32(arena + 156u);
    assert(delivered != 0u && rd32(arena + 168u) == 184u);
    assert(rd32(arena + 172u) == 0u);
    assert(arena[128u + 28u + 10u] == IOS27_MACH_MOVE_SEND);
    assert(memcmp(arena + 184u, before + 320u, 48u) == 0);
    assert(ios27_mach_v2_rights(s, 1u, delivered, &sends, &once, &receive) ==
           IOS27_MACH_OK && sends == 1u);
    assert(ios27_mach_v2_drop_send(s, 1u, delivered) == IOS27_MACH_OK);
    assert(ios27_mach_v2_destroy(s, 3u, item) == IOS27_MACH_OK);
    ios27_mach_v2_free(s);
}
static void phase18c_reject_unsupported_ool_semantics(void) {
    ios27_mach_v2 *s = ios27_mach_v2_new();
    uint8_t arena[512] = {0}, before[512];
    uint32_t port, send, sends, once;
    unsigned receive;
    assert(s);
    assert(ios27_mach_v2_allocate(s, 1u, &port) == IOS27_MACH_OK);
    assert(ios27_mach_v2_make_send(s, 1u, port, 2u, &send) == IOS27_MACH_OK);
    wire(arena + 32u, send, IOS27_MACH_COMPLEX | IOS27_MACH_MOVE_SEND,
         0u, 1882u);
    le32(arena + 36u, 44u);
    le32(arena + 56u, 1u);
    le32(arena + 60u, 300u);
    arena[68u] = 1u; /* Unsupported deallocate */
    arena[71u] = IOS27_MACH_OOL_DESCRIPTOR;
    le32(arena + 72u, 8u);
    memcpy(before, arena, sizeof(arena));
    assert(ios27_mach_guest_call_offset(s, 2u, IOS27_MACH_GUEST_SEND,
                                        0u, arena, sizeof(arena), 32u,
                                        44u, 0u, 24u) ==
           IOS27_MACH_UNSUPPORTED);
    assert(memcmp(before, arena, sizeof(arena)) == 0);
    assert(ios27_mach_v2_rights(s, 2u, send, &sends, &once, &receive) ==
           IOS27_MACH_OK && sends == 1u);
    arena[68u] = 0u;
    le32(arena + 72u, IOS27_MACH_OOL_MAX + 1u);
    assert(ios27_mach_guest_call_offset(s, 2u, IOS27_MACH_GUEST_SEND,
                                        0u, arena, sizeof(arena), 32u,
                                        44u, 0u, 24u) ==
           IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_v2_rights(s, 2u, send, &sends, &once, &receive) ==
           IOS27_MACH_OK && sends == 1u);
    le32(arena + 72u, 8u);
    le32(arena + 60u, UINT32_MAX);
    le32(arena + 64u, UINT32_MAX);
    assert(ios27_mach_guest_call_offset(s, 2u, IOS27_MACH_GUEST_SEND,
                                        0u, arena, sizeof(arena), 32u,
                                        44u, 0u, 24u) ==
           IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_v2_rights(s, 2u, send, &sends, &once, &receive) ==
           IOS27_MACH_OK && sends == 1u);
    ios27_mach_v2_free(s);
}

int main(void) {
    phase18c_bounded_ool_mixed_transfer();
    phase18c_reject_unsupported_ool_semantics();
    phase18c_guest_rejects_host_alias();
    phase18c_guest_portset_and_trailer();
    phase17c_complex_guest_offsets();
    reject_unaligned_host_result();
    round_trip();
    invalid_arena_offsets();
    offset_result_round_trip();
    offset_result_rollback();
    puts("bounded guest arena mach boundary tests passed");
    return 0;
}
