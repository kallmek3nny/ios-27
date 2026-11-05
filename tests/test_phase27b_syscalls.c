#include "ios27/phase27b_syscalls.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BASE 256u
#define SRC 512u
#define DST 1024u
#define STAT 1536u
#define TV 2048u
#define CALL(ctx, id, a, b, c, d) \
    ios27_27b_syscall((ctx), (id), (a), (b), (c), (d), 0, 0)

int main(void) {
    ios27_20e_ctx p;
    int64_t fd;
    uint8_t *stat;
    int64_t seconds;
    ios27_20e_init(&p);
    memcpy(p.guest + BASE, "/tmp/phase27b", sizeof("/tmp/phase27b"));
    memcpy(p.guest + SRC, "test data", 9u);
    assert(CALL(&p, 463u, 42u, BASE, 0x202u, 0u) == -1);
    assert(p.guest_errno == 45);
    fd = CALL(&p, 463u, UINT64_MAX - 1u, BASE, 0x202u, 0u);
    assert(fd == 3);
    assert(CALL(&p, 92u, (uint64_t)fd, 3u, 0u, 0u) == 2);
    assert(CALL(&p, 92u, (uint64_t)fd, 0u, 0u, 0u) == -1);
    assert(p.guest_errno == 45);
    assert(CALL(&p, 4u, (uint64_t)fd, SRC, 9u, 0u) == 9);
    assert(CALL(&p, 153u, (uint64_t)fd, DST, 9u, 0u) == 9);
    assert(memcmp(p.guest + DST, "test data", 9u) == 0);
    assert(CALL(&p, 199u, (uint64_t)fd, 0u, 1u, 0u) == 9);
    assert(CALL(&p, 154u, (uint64_t)fd, SRC, 4u, 0u) == 4);
    assert(CALL(&p, 199u, (uint64_t)fd, 0u, 1u, 0u) == 9);
    assert(CALL(&p, 189u, (uint64_t)fd, STAT, 0u, 0u) == 0);
    stat = p.guest + STAT;
    assert(stat[4] == 0xa4 && stat[5] == 0x81);
    assert(stat[96] == 9 && stat[97] == 0);
    assert(CALL(&p, 116u, TV, 0u, 0u, 0u) == 0);
    memcpy(&seconds, p.guest + TV, sizeof(seconds));
    assert(seconds == 1700000000);
    assert(CALL(&p, 189u, (uint64_t)fd, IOS27_20E_BYTES - 8u, 0u, 0u) == -1);
    assert(p.guest_errno == IOS27_20E_EFAULT);
    assert(CALL(&p, 6u, (uint64_t)fd, 0u, 0u, 0u) == 0);
    assert(CALL(&p, 153u, (uint64_t)fd, DST, 9u, 0u) == -1);
    assert(p.guest_errno == IOS27_20E_EBADF);
    memcpy(p.guest + BASE, "/etc/passwd", sizeof("/etc/passwd"));
    assert(CALL(&p, 463u, UINT64_MAX - 1u, BASE, 0x202u, 0u) == -1);
    assert(p.guest_errno == IOS27_20E_EACCES);
    assert(CALL(&p, 9999u, 0u, 0u, 0u, 0u) == -1);
    assert(p.guest_errno == IOS27_20E_ENOSYS);
    puts("phase27b: openat, positioned I/O, stat, fcntl, time and rejection passed");
    return 0;
}
