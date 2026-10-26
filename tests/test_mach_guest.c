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
int main(void) {
    round_trip();
    invalid_arena_offsets();
    puts("bounded guest arena mach boundary tests passed");
    return 0;
}
