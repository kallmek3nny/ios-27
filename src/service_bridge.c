#include "service_bridge_internal.h"
#include "ios27/libsystem20e_services.h"

#include <stddef.h>

static ios27_20e_ctx services;

_Static_assert(IOS27_SERVICE_ARENA_BYTES == IOS27_20E_BYTES, "service arena ABI");
#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(ios27_service_api) == 32u, "64-bit service table ABI");
_Static_assert(offsetof(ios27_service_api, syscall) == 8u, "service call ABI");
_Static_assert(offsetof(ios27_service_api, memory) == 16u, "service memory ABI");
_Static_assert(offsetof(ios27_service_api, error) == 24u, "service errno ABI");
#endif

static int64_t service_call(uint64_t number, uint64_t a0, uint64_t a1,
                             uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    /* Do not let a wide unsupported selector wrap into an allowed call. */
    if (number > UINT32_MAX) {
        services.guest_errno = IOS27_20E_ENOSYS;
        return -1;
    }
    return ios27_20e_syscall(&services, (uint32_t)number, a0, a1, a2, a3, a4, a5);
}

static void *service_memory(uint64_t offset, uint64_t length, uint32_t write_access) {
    void *span = NULL;
    if (write_access <= 1u)
        span = ios27_20e_guest_ptr(&services, offset, length, (int)write_access);
    if (!span) services.guest_errno = IOS27_20E_EFAULT;
    return span;
}

static int32_t service_error(void) {
    return (int32_t)services.guest_errno;
}

static const ios27_service_api api = {
    IOS27_SERVICE_ABI_VERSION, sizeof(ios27_service_api),
    service_call, service_memory, service_error
};

const ios27_service_api *ios27_service_bridge_initialize(void) {
    ios27_20e_init(&services);
    return &api;
}
