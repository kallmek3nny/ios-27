#include "ios27/libsystem_user.h"
#include <stdlib.h>
#include <string.h>

typedef struct { void *ptr; size_t bytes; } heap_entry;
struct ios27_libsystem {
    uint32_t pid;
    uint8_t arena[IOS27_LIBSYSTEM_GUEST_BYTES];
    uint8_t input[IOS27_LIBSYSTEM_MAX_IO];
    size_t input_count, input_at;
    uint8_t output[2][IOS27_LIBSYSTEM_MAX_IO];
    size_t output_count[2];
    uint8_t fd_open[3];
    heap_entry heap[IOS27_LIBSYSTEM_MAX_ALLOCS];
    size_t heap_count, heap_used;
};
static ios27_libsystem_result good(int64_t value) {
    ios27_libsystem_result r = {value, 0};
    return r;
}
static ios27_libsystem_result bad(int err) {
    ios27_libsystem_result r = {-1, err};
    return r;
}
int ios27_libsystem_create(ios27_libsystem **out, uint32_t guest_pid) {
    ios27_libsystem *ctx;
    if (!out || !guest_pid) return -1;
    *out = NULL;
    ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return -1;
    ctx->pid = guest_pid;
    ctx->fd_open[0] = ctx->fd_open[1] = ctx->fd_open[2] = 1u;
    *out = ctx;
    return 0;
}
void ios27_libsystem_destroy(ios27_libsystem *ctx) {
    if (!ctx) return;
    for (size_t i = 0; i < ctx->heap_count; ++i) free(ctx->heap[i].ptr);
    free(ctx);
}
uint8_t *ios27_libsystem_guest(ios27_libsystem *ctx, size_t offset,
                               size_t length) {
    if (!ctx || offset > IOS27_LIBSYSTEM_GUEST_BYTES ||
        length > IOS27_LIBSYSTEM_GUEST_BYTES - offset) return NULL;
    return ctx->arena + offset;
}
int ios27_libsystem_stdin(ios27_libsystem *ctx, const void *bytes, size_t length) {
    if (!ctx || (length && !bytes) || length > IOS27_LIBSYSTEM_MAX_IO)
        return -1;
    if (length) memcpy(ctx->input, bytes, length);
    ctx->input_count = length;
    ctx->input_at = 0;
    return 0;
}
size_t ios27_libsystem_output(const ios27_libsystem *ctx, int fd,
                              void *out, size_t cap) {
    size_t n;
    if (!ctx || fd < 1 || fd > 2 || (cap && !out)) return 0;
    n = ctx->output_count[fd - 1];
    if (n > cap) n = cap;
    if (n) memcpy(out, ctx->output[fd - 1], n);
    return n;
}
static ios27_libsystem_result io(ios27_libsystem *ctx, int write,
                                 const uint64_t args[6]) {
    uint64_t rawfd = args[0], off = args[1], count = args[2];
    uint8_t *memory;
    size_t n;
    if (rawfd > 2u || !ctx->fd_open[rawfd] ||
        (write ? rawfd == 0u : rawfd != 0u)) return bad(IOS27_DARWIN_EBADF);
    /* Enforce a per-trap copy ceiling and refuse any split/partial write.
     * Count zero is legal even at the one-past-end offset. */
    if (count > IOS27_LIBSYSTEM_MAX_IO) return bad(IOS27_DARWIN_EINVAL);
    if (off > IOS27_LIBSYSTEM_GUEST_BYTES ||
        count > IOS27_LIBSYSTEM_GUEST_BYTES - off)
        return bad(IOS27_DARWIN_EFAULT);
    memory = ctx->arena + (size_t)off;
    n = (size_t)count;
    if (write) {
        size_t id = (size_t)rawfd - 1u;
        if (n > IOS27_LIBSYSTEM_MAX_IO - ctx->output_count[id])
            return bad(IOS27_DARWIN_ENOMEM);
        memcpy(ctx->output[id] + ctx->output_count[id], memory, n);
        ctx->output_count[id] += n;
    } else {
        size_t available = ctx->input_count - ctx->input_at;
        if (n > available) n = available;
        memcpy(memory, ctx->input + ctx->input_at, n);
        ctx->input_at += n;
    }
    return good((int64_t)n);
}
ios27_libsystem_result ios27_libsystem_syscall(ios27_libsystem *ctx,
                                               uint32_t trap,
                                               const uint64_t args[6]) {
    uint32_t number;
    if (!ctx || !args) return bad(IOS27_DARWIN_EINVAL);
    if ((trap & 0xff000000u) != IOS27_DARWIN_CLASS_UNIX ||
        (trap & 0x00ff0000u) != 0u) return bad(IOS27_DARWIN_ENOSYS);
    number = trap & 0xffffu;
    switch (number) {
        case IOS27_DARWIN_SYS_READ: return io(ctx, 0, args);
        case IOS27_DARWIN_SYS_WRITE: return io(ctx, 1, args);
        case IOS27_DARWIN_SYS_CLOSE:
            if (args[0] > 2u || !ctx->fd_open[args[0]])
                return bad(IOS27_DARWIN_EBADF);
            ctx->fd_open[args[0]] = 0u;
            return good(0);
        case IOS27_DARWIN_SYS_GETPID: return good(ctx->pid);
        case IOS27_DARWIN_SYS_GETUID:
        case IOS27_DARWIN_SYS_GETEUID:
        case IOS27_DARWIN_SYS_GETGID:
        case IOS27_DARWIN_SYS_GETEGID: return good(501); /* fake guest identity */
        default: return bad(IOS27_DARWIN_ENOSYS);
    }
}
void *ios27_libsystem_malloc(ios27_libsystem *ctx, size_t bytes) {
    void *ptr;
    if (!ctx || !bytes || bytes > IOS27_LIBSYSTEM_MAX_HEAP - ctx->heap_used ||
        ctx->heap_count == IOS27_LIBSYSTEM_MAX_ALLOCS) return NULL;
    ptr = calloc(1, bytes);
    if (!ptr) return NULL;
    ctx->heap[ctx->heap_count].ptr = ptr;
    ctx->heap[ctx->heap_count++].bytes = bytes;
    ctx->heap_used += bytes;
    return ptr;
}
int ios27_libsystem_free(ios27_libsystem *ctx, void *ptr) {
    if (!ctx || !ptr) return -1;
    for (size_t i = 0; i < ctx->heap_count; ++i) {
        if (ctx->heap[i].ptr == ptr) {
            ctx->heap_used -= ctx->heap[i].bytes;
            free(ptr);
            ctx->heap[i] = ctx->heap[--ctx->heap_count];
            ctx->heap[ctx->heap_count].ptr = NULL;
            return 0;
        }
    }
    return -1;
}
void ios27_libsystem_heap_state(const ios27_libsystem *ctx,
                                size_t *used, size_t *allocations) {
    if (used) *used = ctx ? ctx->heap_used : 0;
    if (allocations) *allocations = ctx ? ctx->heap_count : 0;
}
