#ifndef IOS27_DARWIN_PROBE_H
#define IOS27_DARWIN_PROBE_H

#include "ios27/macho.h"
#include <stddef.h>
#include <stdint.h>

#define IOS27_SYSCALL_CLASS_MACH 0x01000000u
#define IOS27_SYSCALL_CLASS_BSD  0x02000000u

/* Only MOVZ/MOVK/NOP/SVC/RET are interpreted. No dyld, Mach IPC,
 * branches, filesystem writes, host shell calls, or kernel changes. */
typedef struct {
    uint64_t x0;
    uint64_t x16;
    size_t instructions;
    size_t syscalls;
    unsigned carry : 1;
    unsigned exited : 1;
} ios27_darwin_probe_result;

/* Darwin-style SVC result: x0 is errno and carry is set on unsupported
 * syscall, rather than silently forwarding to the Linux/Android kernel.
 * class-0 BSD numbers are accepted for synthetic compatibility fixtures. */
void ios27_darwin_dispatch(uint64_t x16, uint64_t x0,
                           uint64_t *new_x0, unsigned *carry,
                           unsigned *exited);

int ios27_darwin_probe(const uint8_t *data, size_t size,
                       const ios27_macho_info *info,
                       ios27_darwin_probe_result *result,
                       char *error, size_t error_size);

#endif
