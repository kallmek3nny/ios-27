#include "ios27/darwin_shim.h"
#include "darwin_shim_internal.h"

#include <limits.h>
#include <stddef.h>

#if defined(__APPLE__)
__attribute__((used, section("__DATA,__ios27_api"), aligned(8), visibility("hidden")))
#else
__attribute__((used, aligned(8)))
#endif
ios27_service_binding_slot ios27_shim_binding = {
    IOS27_SERVICE_BINDING_VERSION, IOS27_SERVICE_BINDING_BYTES, NULL
};

static int32_t guest_errno;
#define SCRATCH 256u
#define IO_LIMIT 4096u
#define EFAULT_VALUE 14
#define EINVAL_VALUE 22
#define ENOSYS_VALUE 78
#define EOVERFLOW_VALUE 84

int32_t *__error(void) { return &guest_errno; }

static const ios27_service_api *api(void) {
    const ios27_service_api *value = ios27_shim_binding.api;
    if (ios27_shim_binding.version != IOS27_SERVICE_BINDING_VERSION ||
        ios27_shim_binding.size != IOS27_SERVICE_BINDING_BYTES || !value ||
        value->version != IOS27_SERVICE_ABI_VERSION || value->size != sizeof(*value) ||
        !value->syscall || !value->memory || !value->error) {
        guest_errno = ENOSYS_VALUE;
        return NULL;
    }
    return value;
}

static int64_t invoke(const ios27_service_api *service, uint64_t number,
                       uint64_t a0, uint64_t a1, uint64_t a2,
                       uint64_t a3, uint64_t a4, uint64_t a5) {
    int64_t result = service->syscall(number, a0, a1, a2, a3, a4, a5);
    if (result == -1) guest_errno = service->error();
    return result;
}

/* Classify service-arena pointers without undefined cross-object pointer
 * comparisons. Ordinary client buffers are trusted C pointers in the guest
 * image/stack; invalid such pointers can fault the supervised child. */
static int arena_offset(const ios27_service_api *service, const void *pointer,
                         uint64_t *offset) {
    const void *first = service->memory(1u, 0u, 0u);
    uintptr_t base, address = (uintptr_t)pointer;
    if (!first) { guest_errno = EFAULT_VALUE; return -1; }
    base = (uintptr_t)first - 1u;
    if (address >= base && address - base <= IOS27_SERVICE_ARENA_BYTES) {
        *offset = (uint64_t)(address - base);
        return 1;
    }
    return 0;
}

static void *access_buffer(const ios27_service_api *service, const void *pointer,
                            uint64_t length, uint32_t writing) {
    uint64_t offset;
    int location;
    if (!pointer || length > UINTPTR_MAX - (uintptr_t)pointer) {
        guest_errno = EFAULT_VALUE;
        return NULL;
    }
    location = arena_offset(service, pointer, &offset);
    if (location < 0) return NULL;
    if (location) {
        /* The lower arena is private shim scratch/model allocation space;
         * pointers returned by this shim's mmap live in the upper half. */
        if (offset < IOS27_SERVICE_ARENA_BYTES / 2u) {
            guest_errno = EFAULT_VALUE;
            return NULL;
        }
        void *span = service->memory(offset, length, writing);
        if (!span) guest_errno = EFAULT_VALUE;
        return span;
    }
    return (void *)pointer;
}

int32_t getpid(void) {
    const ios27_service_api *service = api();
    int64_t result;
    if (!service) return -1;
    result = invoke(service, 20, 0, 0, 0, 0, 0, 0);
    if (result > INT32_MAX) { guest_errno = EOVERFLOW_VALUE; return -1; }
    return (int32_t)result;
}

int32_t open(const char *path, int32_t flags, ...) {
    const ios27_service_api *service = api();
    char *scratch;
    if (!service) return -1;
    scratch = service->memory(SCRATCH, 96u, 1u);
    if (!scratch) { guest_errno = EFAULT_VALUE; return -1; }
    for (uint64_t i = 0; i < 96u; ++i) {
        const char *source;
        if ((uintptr_t)path > UINTPTR_MAX - i) break;
        source = access_buffer(service, (const void *)((uintptr_t)path + i), 1u, 0u);
        if (!source) return -1;
        scratch[i] = *source;
        if (!scratch[i])
            return (int32_t)invoke(service, 5, SCRATCH,
                                    (uint64_t)(int64_t)flags, 0, 0, 0, 0);
    }
    guest_errno = EFAULT_VALUE;
    return -1;
}

int64_t read(int32_t fd, void *buffer, uint64_t count) {
    const ios27_service_api *service = api();
    uint8_t *destination, *scratch;
    int64_t result;
    if (!service) return -1;
    if (count > IO_LIMIT) { guest_errno = EINVAL_VALUE; return -1; }
    if (!count) return invoke(service, 3, (uint64_t)(int64_t)fd, SCRATCH, 0, 0, 0, 0);
    destination = access_buffer(service, buffer, count, 1u);
    if (!destination) return -1;
    scratch = service->memory(SCRATCH, count, 1u);
    if (!scratch) { guest_errno = EFAULT_VALUE; return -1; }
    result = invoke(service, 3, (uint64_t)(int64_t)fd, SCRATCH, count, 0, 0, 0);
    if (result > (int64_t)count) { guest_errno = EOVERFLOW_VALUE; return -1; }
    if (result > 0)
        for (uint64_t i = 0; i < (uint64_t)result; ++i) destination[i] = scratch[i];
    return result;
}

int64_t write(int32_t fd, const void *buffer, uint64_t count) {
    const ios27_service_api *service = api();
    const uint8_t *source;
    uint8_t *scratch;
    int64_t result;
    if (!service) return -1;
    if (count > IO_LIMIT) { guest_errno = EINVAL_VALUE; return -1; }
    if (!count) return invoke(service, 4, (uint64_t)(int64_t)fd, SCRATCH, 0, 0, 0, 0);
    source = access_buffer(service, buffer, count, 0u);
    if (!source) return -1;
    scratch = service->memory(SCRATCH, count, 1u);
    if (!scratch) { guest_errno = EFAULT_VALUE; return -1; }
    for (uint64_t i = 0; i < count; ++i) scratch[i] = source[i];
    result = invoke(service, 4, (uint64_t)(int64_t)fd, SCRATCH, count, 0, 0, 0);
    if (result > (int64_t)count) { guest_errno = EOVERFLOW_VALUE; return -1; }
    return result;
}

int32_t close(int32_t fd) {
    const ios27_service_api *service = api();
    return service ? (int32_t)invoke(service, 6, (uint64_t)(int64_t)fd, 0, 0, 0, 0, 0) : -1;
}

int64_t lseek(int32_t fd, int64_t offset, int32_t whence) {
    const ios27_service_api *service = api();
    return service ? invoke(service, 199, (uint64_t)(int64_t)fd,
                             (uint64_t)offset, (uint64_t)(int64_t)whence, 0, 0, 0) : -1;
}

void *mmap(void *hint, uint64_t length, int32_t protection, int32_t flags,
           int32_t fd, int64_t offset) {
    const ios27_service_api *service = api();
    int64_t result;
    void *pointer;
    if (!service) return IOS27_DARWIN_MAP_FAILED;
    if (hint || !length || length > IOS27_SERVICE_ARENA_BYTES / 2u) {
        guest_errno = EINVAL_VALUE;
        return IOS27_DARWIN_MAP_FAILED;
    }
    result = invoke(service, 197, 0, length, (uint64_t)(int64_t)protection,
                      (uint64_t)(int64_t)flags, (uint64_t)(int64_t)fd, (uint64_t)offset);
    if (result < 0) return IOS27_DARWIN_MAP_FAILED;
    pointer = service->memory((uint64_t)result, 0u, 0u);
    if (!pointer) {
        /* This cannot occur with the supported model after a successful map.
         * Roll back before reporting a malformed service response. */
        uint64_t rounded = (length + 4095u) & ~UINT64_C(4095);
        (void)invoke(service, 73, (uint64_t)result, rounded, 0, 0, 0, 0);
        guest_errno = EFAULT_VALUE;
        return IOS27_DARWIN_MAP_FAILED;
    }
    return pointer;
}

static int32_t protect_or_unmap(uint64_t operation, void *address,
                                 uint64_t length, int32_t protection) {
    const ios27_service_api *service = api();
    uint64_t offset;
    if (!service) return -1;
    if (arena_offset(service, address, &offset) != 1 ||
        !offset || offset >= IOS27_SERVICE_ARENA_BYTES ||
        length > IOS27_SERVICE_ARENA_BYTES - offset) {
        guest_errno = EINVAL_VALUE;
        return -1;
    }
    return (int32_t)invoke(service, operation, offset, length,
                            (uint64_t)(int64_t)protection, 0, 0, 0);
}

int32_t mprotect(void *address, uint64_t length, int32_t protection) {
    return protect_or_unmap(74, address, length, protection);
}

int32_t munmap(void *address, uint64_t length) {
    return protect_or_unmap(73, address, length, 0);
}
