#ifndef IOS27_PHASE22B_SYSCALLS_H
#define IOS27_PHASE22B_SYSCALLS_H
#include <stddef.h>
#include <stdint.h>
#include "ios27/libsystem20e_services.h"

/* Research-only bounded Darwin/BSD syscall adapter. All guest addresses are
   offsets into an owned ios27_20e_ctx. No pointers are sent to host syscalls. */
#define IOS27_22B_BSD UINT32_C(0x02000000)
/* The following clock helpers are PRIVATE SERVICE SELECTORS, NOT Darwin syscalls. */
#define IOS27_22B_PRIVATE UINT32_C(0x7f000000)
#define IOS27_22B_FDS 16u
#define IOS27_22B_CAPS 8u
#define IOS27_22B_CAP_BYTES 4096u
#define IOS27_22B_IOV_MAX 16u
#define IOS27_22B_READ 3u
#define IOS27_22B_WRITE 4u
#define IOS27_22B_OPEN 5u
#define IOS27_22B_CLOSE 6u
#define IOS27_22B_GETPID 20u
#define IOS27_22B_GETUID 24u
#define IOS27_22B_GETEUID 25u
#define IOS27_22B_GETEGID 43u
#define IOS27_22B_GETGID 47u
#define IOS27_22B_GETTIMEOFDAY 116u
#define IOS27_22B_READV 120u
#define IOS27_22B_WRITEV 121u
#define IOS27_22B_LSEEK 199u
#define IOS27_22B_CLOCK_GETTIME 1u
#define IOS27_22B_NANOSLEEP 2u
#define IOS27_22B_THREAD_SELFID 372u
#define IOS27_22B_EINTR 4
#define IOS27_22B_EBADF 9
#define IOS27_22B_EAGAIN 35
#define IOS27_22B_EACCES 13
#define IOS27_22B_EFAULT 14
#define IOS27_22B_EINVAL 22
#define IOS27_22B_EMFILE 24
#define IOS27_22B_ENOSPC 28
#define IOS27_22B_ENOENT 2
#define IOS27_22B_EOVERFLOW 84
#define IOS27_22B_ENOSYS 78
#define IOS27_22B_CLOCK_REALTIME 0u
#define IOS27_22B_CLOCK_MONOTONIC 6u
#define IOS27_22B_O_RDONLY 0u
#define IOS27_22B_O_WRONLY 1u
#define IOS27_22B_O_RDWR 2u
#define IOS27_22B_O_APPEND 8u

typedef struct ios27_22b_context ios27_22b_context;
typedef struct {
    int64_t x0; /* Darwin user-visible return register: errno on failure. */
    uint8_t carry; /* Darwin error condition flag (1 on failure). */
    int darwin_errno;
} ios27_22b_result;
typedef struct {
    uint64_t size;
    uint64_t position;
    uint32_t mode; /* read/write capability mask, not a Darwin stat struct */
    uint32_t resource_id;
} ios27_22b_metadata;

/* The supplied guest context must outlive this object, and its guest arena
   must not be accessed outside this adapter concurrently with syscalls. */
int ios27_22b_create(ios27_22b_context **out, ios27_20e_ctx *guest,
                     uint32_t pid, uint32_t uid, uint32_t gid);
void ios27_22b_destroy(ios27_22b_context *ctx); /* join clients before destroy */
/* Register an exact /cap/<name> read-only or writable in-memory capability. */
int ios27_22b_preopen(ios27_22b_context *ctx, const char *name,
                      const void *data, size_t len, int writable);
int ios27_22b_stdin(ios27_22b_context *ctx, const void *data, size_t len);
size_t ios27_22b_capture(ios27_22b_context *ctx, int fd, void *out, size_t cap);
int ios27_22b_set_clock(ios27_22b_context *ctx, uint64_t realtime_ns,
                        uint64_t monotonic_ns);
int ios27_22b_advance(ios27_22b_context *ctx, uint64_t ns);
void ios27_22b_interrupt(ios27_22b_context *ctx);
int ios27_22b_errno(ios27_22b_context *ctx);
int ios27_22b_metadata_fd(ios27_22b_context *ctx, int fd,
                          ios27_22b_metadata *out);
ios27_22b_result ios27_22b_dispatch(ios27_22b_context *ctx, uint32_t trap,
                                      const uint64_t args[6]);
#endif