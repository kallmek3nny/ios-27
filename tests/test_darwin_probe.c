#include "ios27/darwin_probe.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ENTRY_OFF 256u
#define IMAGE_SIZE 320u

static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0u; i < 4u; ++i) p[i] = (uint8_t)(v >> (i * 8u));
}
static void put64(uint8_t *p, uint64_t v) {
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}
static void fixture(uint8_t b[IMAGE_SIZE]) {
    memset(b, 0, IMAGE_SIZE);
    put32(b, 0xfeedfacfu);
    put32(b + 4, 0x0100000cu);
    put32(b + 12, 2u);
    put32(b + 16, 2u);
    put32(b + 20, 96u);
    put32(b + 32, 0x19u);
    put32(b + 36, 72u);
    memcpy(b + 40, "__TEXT", 6u);
    put64(b + 56, 0x100000000ull);
    put64(b + 64, 4096u);
    put64(b + 72, 0u);
    put64(b + 80, IMAGE_SIZE);
    put32(b + 88, 5u);
    put32(b + 92, 5u);
    put32(b + 104, 0x80000028u);
    put32(b + 108, 24u);
    put64(b + 112, ENTRY_OFF);
}

static uint32_t movz(unsigned r, unsigned imm) {
    return 0xd2800000u | ((imm & 0xffffu) << 5u) | (r & 31u);
}
static uint32_t movk(unsigned r, unsigned imm, unsigned shift) {
    return 0xf2800000u | (((shift / 16u) & 3u) << 21u) |
           ((imm & 0xffffu) << 5u) | (r & 31u);
}

int main(void) {
    uint8_t b[IMAGE_SIZE];
    ios27_macho_info info;
    ios27_darwin_probe_result result;
    char error[128];
    uint64_t retval;
    unsigned carry, exited;

    /* Darwin BSD getpid(20), with the BSD syscall class in x16. */
    fixture(b);
    put32(b + ENTRY_OFF, movz(16u, 20u));
    put32(b + ENTRY_OFF + 4u, movk(16u, 0x200u, 16u));
    put32(b + ENTRY_OFF + 8u, 0xd4001001u);
    put32(b + ENTRY_OFF + 12u, 0xd65f03c0u);
    assert(ios27_macho_parse(b, sizeof(b), &info, error, sizeof(error)) == 0);
    assert(ios27_darwin_probe(b, sizeof(b), &info, &result, error, sizeof(error)) == 0);
    assert(result.x0 == (uint64_t)getpid());
    assert(result.syscalls == 1u && !result.carry && !result.exited);
    assert(result.instructions == 4u);

    /* Guest exit is virtual and must not terminate the test host. */
    fixture(b);
    put32(b + ENTRY_OFF, movz(0u, 17u));
    put32(b + ENTRY_OFF + 4u, movz(16u, 1u));
    put32(b + ENTRY_OFF + 8u, 0xd4001001u);
    assert(ios27_macho_parse(b, sizeof(b), &info, error, sizeof(error)) == 0);
    assert(ios27_darwin_probe(b, sizeof(b), &info, &result, error, sizeof(error)) == 0);
    assert(result.x0 == 17u && result.exited);

    /* No filesystem syscall reaches the host. */
    ios27_darwin_dispatch(IOS27_SYSCALL_CLASS_BSD | 5u, 0u,
                          &retval, &carry, &exited); /* open */
    assert(retval == (uint64_t)ENOSYS && carry == 1u && exited == 0u);
    ios27_darwin_dispatch(IOS27_SYSCALL_CLASS_MACH | 10u, 0u,
                          &retval, &carry, &exited);
    assert(retval == (uint64_t)ENOSYS && carry == 1u);
    ios27_darwin_dispatch(IOS27_SYSCALL_CLASS_BSD | 4u, 0u,
                          &retval, &carry, &exited); /* write */
    assert(retval == (uint64_t)ENOSYS && carry == 1u);

    fixture(b);
    put32(b + ENTRY_OFF, 0x14000000u); /* unsupported branch */
    assert(ios27_macho_parse(b, sizeof(b), &info, error, sizeof(error)) == 0);
    assert(ios27_darwin_probe(b, sizeof(b), &info, &result, error, sizeof(error)) != 0);
    info.needs_dyld = 1u;
    assert(ios27_darwin_probe(b, sizeof(b), &info, &result, error, sizeof(error)) != 0);
    puts("darwin probe tests passed");
    return 0;
}
