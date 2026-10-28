#ifndef IOS27_LIBSYSTEM_USER_H
#define IOS27_LIBSYSTEM_USER_H
#include <stddef.h>
#include <stdint.h>

/* Small Darwin/BSD syscall surface for an explicitly owned guest arena.
 * All guest addresses are offsets into the arena, NOT host pointers. No host
 * filesystem, network, kernel, process creation or implicit clock access. */
#define IOS27_LIBSYSTEM_GUEST_BYTES 65536u
#define IOS27_LIBSYSTEM_MAX_HEAP 131072u
#define IOS27_LIBSYSTEM_MAX_ALLOCS 128u
#define IOS27_LIBSYSTEM_MAX_IO 8192u
#define IOS27_DARWIN_CLASS_UNIX 0x02000000u
#define IOS27_DARWIN_SYS_READ 3u
#define IOS27_DARWIN_SYS_WRITE 4u
#define IOS27_DARWIN_SYS_CLOSE 6u
#define IOS27_DARWIN_SYS_GETPID 20u
#define IOS27_DARWIN_SYS_GETUID 24u
#define IOS27_DARWIN_SYS_GETEUID 25u
#define IOS27_DARWIN_SYS_GETGID 47u
#define IOS27_DARWIN_SYS_GETEGID 43u
#define IOS27_DARWIN_EBADF 9
#define IOS27_DARWIN_ENOMEM 12
#define IOS27_DARWIN_EFAULT 14
#define IOS27_DARWIN_EINVAL 22
#define IOS27_DARWIN_ENOSYS 78

typedef struct ios27_libsystem ios27_libsystem;
typedef struct { int64_t value; int darwin_errno; } ios27_libsystem_result;
int ios27_libsystem_create(ios27_libsystem **out, uint32_t guest_pid);
void ios27_libsystem_destroy(ios27_libsystem *ctx);
uint8_t *ios27_libsystem_guest(ios27_libsystem *ctx, size_t offset,
                               size_t length);
/* Owned, deterministic stdin bytes and captured stdout/stderr. */
int ios27_libsystem_stdin(ios27_libsystem *ctx, const void *bytes, size_t length);
size_t ios27_libsystem_output(const ios27_libsystem *ctx, int fd,
                              void *out, size_t cap);
/* A small fail-closed dispatcher for Darwin BSD trap numbers. */
ios27_libsystem_result ios27_libsystem_syscall(ios27_libsystem *ctx,
                                               uint32_t trap,
                                               const uint64_t args[6]);
/* Heap API tracks ownership, quota, overflow and double-free. */
void *ios27_libsystem_malloc(ios27_libsystem *ctx, size_t bytes);
int ios27_libsystem_free(ios27_libsystem *ctx, void *address);
void ios27_libsystem_heap_state(const ios27_libsystem *ctx,
                                size_t *used_bytes, size_t *allocations);
#endif
