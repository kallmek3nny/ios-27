#ifndef IOS27_SERVICE_BRIDGE_H
#define IOS27_SERVICE_BRIDGE_H

#include <stdint.h>

/* Original, integer-only research ABI for trusted SDK-free ARM64 fixtures.
 * The opt-in services loader passes this table in x4, after the usual
 * argc/argv/envp/apple registers. This is NOT Apple's libSystem ABI and
 * does not intercept SVC instructions or expose host filesystem services.
 *
 * syscall uses raw Darwin BSD numbers and guest-arena offsets, never host
 * pointer arguments. It returns -1 on error; error() returns Darwin errno.
 * memory() returns a checked span in the private 64 KiB service arena.
 * Only that span's requested read/write access is approved. The returned
 * pointer is valid only for the current mapping state and helper lifetime;
 * it must not be retained across mprotect/munmap. It is not a kernel-enforced
 * memory capability. All code remains trusted-fixture-only and single-threaded.
 */
#define IOS27_SERVICE_ABI_VERSION 1u
#define IOS27_SERVICE_ARENA_BYTES 65536u

typedef struct {
    uint32_t version;
    uint32_t size;
    int64_t (*syscall)(uint64_t number, uint64_t a0, uint64_t a1, uint64_t a2,
                        uint64_t a3, uint64_t a4, uint64_t a5);
    void *(*memory)(uint64_t offset, uint64_t length, uint32_t write_access);
    int32_t (*error)(void);
} ios27_service_api;

#endif
