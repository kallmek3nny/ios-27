#include "ios27/phase27b_libsystem.h"
#include "darwin_shim_internal.h"
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

/* Additional named calls for trusted, compiler-linked Darwin clients.
 * Uses the EXISTING v1 binding record and service table. No x4 callback,
 * Linux fd forwarding, dynamic dyld import, or host filesystem access. */
#define SCRATCH_OFFSET 256u
#define MAX_TRANSFER 4096u
#define EFAULT_D 14
#define EINVAL_D 22
#define ENOTSUP_D 45
#define ENOSYS_D 78
#define EOVERFLOW_D 84

static int32_t *error_ptr(void) { return __error(); }

static const ios27_service_api *services(void) {
    const ios27_service_api *s = ios27_shim_binding.api;
    if (ios27_shim_binding.version != IOS27_SERVICE_BINDING_VERSION ||
        ios27_shim_binding.size != IOS27_SERVICE_BINDING_BYTES || !s ||
        s->version != IOS27_SERVICE_ABI_VERSION || s->size != sizeof(*s) ||
        !s->syscall || !s->memory || !s->error) {
        *error_ptr() = ENOSYS_D;
        return NULL;
    }
    return s;
}
static int64_t call(const ios27_service_api *s, uint64_t number,
                    uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    int64_t value = s->syscall(number, a0, a1, a2, a3, 0, 0);
    if (value == -1) *error_ptr() = s->error();
    return value;
}
static void *checked(const ios27_service_api *s, const void *pointer,
                     uint64_t length, uint32_t write_access) {
    uintptr_t address = (uintptr_t)pointer;
    const void *start = s->memory(1u, 0u, 0u);
    uintptr_t base;
    if (!pointer || !start || length > UINTPTR_MAX - address) {
        *error_ptr() = EFAULT_D;
        return NULL;
    }
    base = (uintptr_t)start - 1u;
    if (address >= base && address - base <= IOS27_SERVICE_ARENA_BYTES) {
        uint64_t off = (uint64_t)(address - base);
        void *p;
        if (off < IOS27_SERVICE_ARENA_BYTES / 2u) {
            *error_ptr() = EFAULT_D;
            return NULL;
        }
        p = s->memory(off, length, write_access);
        if (!p) *error_ptr() = EFAULT_D;
        return p;
    }
    /* Like the v1 shim, ordinary trusted stack/constant pointers cannot
     * be proven valid without a guest mapping registry. */
    return (void *)pointer;
}
static int path_to_scratch(const ios27_service_api *s, const char *path) {
    uint8_t *scratch = s->memory(SCRATCH_OFFSET, 96u, 1u);
    if (!scratch) { *error_ptr() = EFAULT_D; return -1; }
    for (uint64_t i = 0u; i < 96u; ++i) {
        const char *p;
        if ((uintptr_t)path > UINTPTR_MAX - i) break;
        p = checked(s, (const void *)((uintptr_t)path + i), 1u, 0u);
        if (!p) return -1;
        scratch[i] = (uint8_t)*p;
        if (!scratch[i]) return 0;
    }
    *error_ptr() = EFAULT_D;
    return -1;
}
int32_t openat(int32_t dirfd, const char *path, int32_t flags, ...) {
    const ios27_service_api *s = services();
    int64_t result;
    if (!s) return -1;
    if (path_to_scratch(s, path)) return -1;
    result = call(s, 463u, (uint64_t)(int64_t)dirfd, SCRATCH_OFFSET,
                  (uint64_t)(int64_t)flags, 0);
    if (result > INT32_MAX) { *error_ptr() = EOVERFLOW_D; return -1; }
    return (int32_t)result;
}
static int64_t transfer(int32_t fd, void *buffer, uint64_t n,
                        int64_t offset, int writing) {
    const ios27_service_api *s = services();
    uint8_t *scratch;
    uint8_t *guest;
    int64_t result;
    if (!s) return -1;
    if (n > MAX_TRANSFER || offset < 0) {
        *error_ptr() = EINVAL_D;
        return -1;
    }
    if (!n) return call(s, writing ? 154u : 153u,
                        (uint64_t)(int64_t)fd, SCRATCH_OFFSET, 0, (uint64_t)offset);
    guest = checked(s, buffer, n, writing ? 0u : 1u);
    if (!guest) return -1;
    scratch = s->memory(SCRATCH_OFFSET, n, 1u);
    if (!scratch) { *error_ptr() = EFAULT_D; return -1; }
    if (writing)
        for (uint64_t i = 0; i < n; ++i) scratch[i] = guest[i];
    result = call(s, writing ? 154u : 153u,
                  (uint64_t)(int64_t)fd, SCRATCH_OFFSET, n, (uint64_t)offset);
    if (result > (int64_t)n) { *error_ptr() = EOVERFLOW_D; return -1; }
    if (!writing && result > 0)
        for (uint64_t i = 0; i < (uint64_t)result; ++i) guest[i] = scratch[i];
    return result;
}
int64_t pread(int32_t fd, void *buffer, uint64_t count, int64_t offset) {
    return transfer(fd, buffer, count, offset, 0);
}
int64_t pwrite(int32_t fd, const void *buffer, uint64_t count, int64_t offset) {
    return transfer(fd, (void *)buffer, count, offset, 1);
}
int32_t fstat(int32_t fd, void *darwin_stat_144) {
    const ios27_service_api *s = services();
    uint8_t *destination, *scratch;
    int64_t result;
    if (!s) return -1;
    destination = checked(s, darwin_stat_144, 144u, 1u);
    if (!destination) return -1;
    scratch = s->memory(SCRATCH_OFFSET, 144u, 1u);
    if (!scratch) { *error_ptr() = EFAULT_D; return -1; }
    result = call(s, 189u, (uint64_t)(int64_t)fd, SCRATCH_OFFSET, 0, 0);
    if (result == 0)
        for (uint64_t i = 0; i < 144u; ++i) destination[i] = scratch[i];
    return (int32_t)result;
}
int32_t fcntl(int32_t fd, int32_t command, ...) {
    const ios27_service_api *s = services();
    int64_t result;
    if (!s) return -1;
    result = call(s, 92u, (uint64_t)(int64_t)fd,
                  (uint64_t)(int64_t)command, 0, 0);
    if (result > INT32_MAX) { *error_ptr() = EOVERFLOW_D; return -1; }
    return (int32_t)result;
}
int32_t gettimeofday(void *timeval_16, void *timezone) {
    const ios27_service_api *s = services();
    uint8_t *destination, *scratch;
    int64_t result;
    if (!s) return -1;
    if (timezone) { *error_ptr() = ENOTSUP_D; return -1; }
    destination = checked(s, timeval_16, 16u, 1u);
    if (!destination) return -1;
    scratch = s->memory(SCRATCH_OFFSET, 16u, 1u);
    if (!scratch) { *error_ptr() = EFAULT_D; return -1; }
    result = call(s, 116u, SCRATCH_OFFSET, 0, 0, 0);
    if (result == 0)
        for (uint64_t i = 0; i < 16u; ++i) destination[i] = scratch[i];
    return (int32_t)result;
}
