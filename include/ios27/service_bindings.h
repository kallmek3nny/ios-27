#ifndef IOS27_SERVICE_BINDINGS_H
#define IOS27_SERVICE_BINDINGS_H

#include "ios27/service_bridge.h"
#include <stddef.h>

#define IOS27_SERVICE_BINDING_VERSION 1u
#define IOS27_SERVICE_BINDING_BYTES 16u

/* One original-source shim slot in __DATA,__ios27_api. The on-disk pointer
 * must be NULL. Only the explicit services loader populates its staged copy;
 * ordinary runners leave it NULL. This is not a general dyld relocation. */
typedef struct {
    uint32_t version;
    uint32_t size;
    const ios27_service_api *api;
} ios27_service_binding_slot;

typedef struct {
    uint64_t vmaddr;
    uint64_t file_offset;
    unsigned present;
} ios27_service_binding_plan;

/* Host-independent, read-only preflight of a freestanding ARM64 MH_EXECUTE.
 * Accepts no slot, or exactly one aligned, writable, non-executable,
 * file-backed 16-byte slot with the supported header and a zero pointer.
 * No host pointer is written and no image is mapped. Failure clears plan. */
int ios27_service_binding_inspect(const uint8_t *bytes, size_t size,
                                   ios27_service_binding_plan *plan,
                                   char *error, size_t error_size);

#endif
