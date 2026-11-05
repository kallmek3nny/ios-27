#include "service_bridge_internal.h"
#include "ios27/libsystem20e_services.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int64_t call3(const ios27_service_api *api, uint64_t number,
                       uint64_t a0, uint64_t a1, uint64_t a2) {
    return api->syscall(number, a0, a1, a2, 0, 0, 0);
}

int main(void) {
    const ios27_service_api *api = ios27_service_bridge_initialize();
    uint8_t *span;
    int64_t fd;
    assert(api && api->version == 1u && api->size == sizeof(*api));
    assert(api->error() == 0);
    assert(call3(api, UINT64_C(0x100000014), 0, 0, 0) == -1);
    assert(api->error() == IOS27_20E_ENOSYS);
    assert(call3(api, 20, 0, 0, 0) == 101 && api->error() == IOS27_20E_ENOSYS);
    assert(!api->memory(1, 1, 2) && api->error() == IOS27_20E_EFAULT);
    assert(!api->memory(0, 1, 0));
    assert(!api->memory(UINT64_MAX, 1, 0));
    assert(!api->memory(IOS27_SERVICE_ARENA_BYTES - 1u, 2, 0));
    span = api->memory(1024, 32, 1);
    assert(span);
    memcpy(span, "/tmp/bridge", sizeof("/tmp/bridge"));
    fd = call3(api, 5, 1024, IOS27_20E_RDWR | IOS27_20E_CREAT, 0);
    assert(fd == 3);
    memcpy(span + 16, "hello", 5);
    assert(call3(api, 4, (uint64_t)fd, 1040, 5) == 5);
    assert(call3(api, 199, (uint64_t)fd, 0, 0) == 0);
    assert(call3(api, 3, (uint64_t)fd, 2048, 5) == 5);
    assert(!memcmp(api->memory(2048, 5, 0), "hello", 5));
    assert(call3(api, 6, (uint64_t)fd, 0, 0) == 0);
    assert(call3(api, 6, UINT64_MAX, 0, 0) == -1 &&
           api->error() == IOS27_20E_EBADF);
    /* The helper initialization discards all earlier virtual resources. */
    assert(ios27_service_bridge_initialize() == api && api->error() == 0);
    span = api->memory(1024, 32, 1);
    assert(span && span[0] == 0);
    memcpy(span, "/tmp/bridge", sizeof("/tmp/bridge"));
    assert(call3(api, 5, 1024, 0, 0) == -1 && api->error() == IOS27_20E_ENOENT);
    puts("service table: wide selectors, checked spans, virtual file lifecycle and reset passed");
    return 0;
}
