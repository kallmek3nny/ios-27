#include "ios27/phase27b_syscalls.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

/* This adapter preserves the tested phase20e file and VM ownership model.
 * No host syscalls. It is intentionally single-threaded, as is its v1 table.
 * The 26b generation-tagged FD implementation remains a separate model. */
static int64_t fail(ios27_20e_ctx *c, int error) {
    c->guest_errno = error;
    return -1;
}
static int valid_file(ios27_20e_ctx *c, uint64_t handle) {
    return handle >= 3u && handle < IOS27_20E_FDS &&
           c->fds[handle].used && c->fds[handle].kind == 4u;
}
static int64_t positioned(ios27_20e_ctx *c, uint64_t fd,
                          uint64_t address, uint64_t length,
                          uint64_t at, int writing) {
    size_t previous;
    int64_t result;
    if (!valid_file(c, fd)) return fail(c, IOS27_20E_EBADF);
    if (at > IOS27_20E_FILE_BYTES || length > IOS27_20E_FILE_BYTES ||
        (writing && (c->fds[fd].mode & IOS27_20E_APPEND)))
        return fail(c, IOS27_20E_EINVAL);
    previous = c->fds[fd].pos;
    c->fds[fd].pos = (size_t)at;
    result = writing ? ios27_20e_write(c, (int)fd, address, length) :
                       ios27_20e_read(c, (int)fd, address, length);
    c->fds[fd].pos = previous; /* Even on error, never change file offset. */
    return result;
}
static int64_t stat_file(ios27_20e_ctx *c, uint64_t fd, uint64_t address) {
    uint8_t bytes[144] = {0};
    uint64_t size;
    void *destination;
    if (!valid_file(c, fd)) return fail(c, IOS27_20E_EBADF);
    destination = ios27_20e_guest_ptr(c, address, sizeof(bytes), 1);
    if (!destination) return fail(c, IOS27_20E_EFAULT);
    size = (uint64_t)c->files[c->fds[fd].pad].len;
    bytes[4] = 0xa4; bytes[5] = 0x81; /* Darwin inode64 S_IFREG | 0644 */
    bytes[6] = 1; bytes[8] = (uint8_t)(c->fds[fd].pad + 1u);
    bytes[16] = 0xf5; bytes[17] = 1; /* virtual uid 501 */
    bytes[20] = 20;                 /* virtual gid 20 */
    for (size_t i = 0; i < 8u; ++i)
        bytes[96u + i] = (uint8_t)(size >> (8u * i));
    bytes[104] = (uint8_t)((size + 511u) / 512u);
    bytes[112] = 0; bytes[113] = 0x10; /* 4096-byte block size */
    memcpy(destination, bytes, sizeof(bytes));
    return 0;
}
int64_t ios27_27b_syscall(ios27_20e_ctx *c, uint32_t selector,
                          uint64_t a0, uint64_t a1, uint64_t a2,
                          uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a4;
    (void)a5;
    if (!c) return -1;
    switch (selector) {
        case 463u: /* openat: only AT_FDCWD; no directory handles */
            if (a0 != UINT64_MAX - 1u)
                return fail(c, 45); /* Darwin ENOTSUP */
            if (a2 > INT_MAX) return fail(c, IOS27_20E_EINVAL);
            return ios27_20e_open(c, a1, (int)a2);
        case 153u: /* pread */
            return positioned(c, a0, a1, a2, a3, 0);
        case 154u: /* pwrite */
            return positioned(c, a0, a1, a2, a3, 1);
        case 189u: /* fstat, audited Darwin inode64 layout */
            return stat_file(c, a0, a1);
        case 92u: /* fcntl, F_GETFL only */
            if (!valid_file(c, a0)) return fail(c, IOS27_20E_EBADF);
            if (a1 != 3u) return fail(c, 45);
            return c->fds[a0].mode;
        default:
            return ios27_20e_syscall(c, selector, a0, a1, a2, a3, a4, a5);
    }
}
