#include "ios27/service_bindings.h"
#include "ios27/macho.h"

#include <stdio.h>
#include <string.h>

#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(ios27_service_binding_slot) == IOS27_SERVICE_BINDING_BYTES,
               "64-bit service binding layout");
#endif

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static uint64_t read64(const uint8_t *p) {
    return (uint64_t)read32(p) | ((uint64_t)read32(p + 4u) << 32u);
}

static int fail(char *error, size_t size, const char *message) {
    if (error && size) (void)snprintf(error, size, "%s", message);
    return -1;
}

int ios27_service_binding_inspect(const uint8_t *bytes, size_t size,
                                   ios27_service_binding_plan *plan,
                                   char *error, size_t error_size) {
    static const char section_name[16] = "__ios27_api";
    static const char segment_name[16] = "__DATA";
    ios27_macho_info info;
    ios27_exec_layout layout;
    ios27_service_binding_plan staged = {0};
    size_t cursor = 32u;
    if (!plan) return fail(error, error_size, "null service binding plan");
    memset(plan, 0, sizeof(*plan));
    /* The generic executable preflight validates command/section geometry,
     * file/VM correspondence, overlap, relocations and all fixup metadata.
     * Any 16 KiB-compatible image is also valid at this 4 KiB granule. */
    if (ios27_macho_parse(bytes, size, &info, error, error_size) ||
        ios27_macho_executable_layout(bytes, size, &info, 4096u, &layout,
                                      error, error_size)) return -1;
    for (uint32_t i = 0; i < read32(bytes + 16u); ++i) {
        const uint8_t *command = bytes + cursor;
        if (read32(command) == 0x19u) {
            for (uint32_t j = 0; j < read32(command + 64u); ++j) {
                const uint8_t *section = command + 72u + (size_t)j * 80u;
                uint64_t vmaddr, offset;
                if (memcmp(section, section_name, sizeof(section_name))) continue;
                if (staged.present)
                    return fail(error, error_size, "duplicate service binding section");
                vmaddr = read64(section + 32u);
                offset = read32(section + 48u);
                if (memcmp(command + 8u, segment_name, sizeof(segment_name)) ||
                    read32(command + 56u) != 3u || read32(command + 60u) != 3u ||
                    read64(section + 40u) != IOS27_SERVICE_BINDING_BYTES ||
                    (vmaddr & 7u) || (offset & 7u) ||
                    read32(section + 52u) < 3u || read32(section + 64u) != 0u ||
                    offset > size || IOS27_SERVICE_BINDING_BYTES > size - offset)
                    return fail(error, error_size, "invalid service binding section");
                const uint8_t *slot = bytes + (size_t)offset;
                if (read32(slot) != IOS27_SERVICE_BINDING_VERSION ||
                    read32(slot + 4u) != IOS27_SERVICE_BINDING_BYTES || read64(slot + 8u))
                    return fail(error, error_size, "invalid or prebound service slot");
                staged.vmaddr = vmaddr;
                staged.file_offset = offset;
                staged.present = 1u;
            }
        }
        cursor += read32(command + 4u);
    }
    *plan = staged;
    if (error && error_size) error[0] = '\0';
    return 0;
}
