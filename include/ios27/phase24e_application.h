#ifndef IOS27_PHASE24E_APPLICATION_H
#define IOS27_PHASE24E_APPLICATION_H

#include <stddef.h>
#include <stdint.h>

#define IOS27_24E_MAX_IMAGES 8u
#define IOS27_24E_MAX_ARGS 32u
#define IOS27_24E_MAX_IMAGE_BYTES (4u * 1024u * 1024u)

typedef enum {
    IOS27_24E_NATIVE_EXECUTED = 0,
    IOS27_24E_METADATA_ONLY = 1,
    IOS27_24E_UNSUPPORTED_HOST = 2,
    IOS27_24E_UNSUPPORTED_ABI = 3,
    IOS27_24E_REJECTED = 4,
    IOS27_24E_LOAD_FAILED = 5,
    IOS27_24E_GUEST_FAILED = 6
} ios27_24e_status;

typedef struct {
    const char *name;       /* unique simple identifier */
    const char *guest_path; /* unique absolute canonical guest path */
    const uint8_t *bytes;   /* trusted original-source Mach-O, never Apple code */
    size_t size;
} ios27_24e_image;

typedef struct {
    const ios27_24e_image *images;
    size_t image_count;
    const char *executable_name;
    const char *executable_path;
    int argc;
    const char *const *argv;
    const char *const *envp;
    const char *const *apple;
    uint64_t expected_guest_operations; /* guest-owned exported counter */
    int expected_exit_code;
    unsigned needs_objc;
    unsigned needs_threads;
    unsigned needs_mach;
    unsigned needs_syscalls;
} ios27_24e_application;

typedef struct {
    ios27_24e_status status;
    uint64_t replay_hash;             /* deterministic, no host pointers */
    uint64_t verified_guest_operations; /* zero except real ARM64 guest entry */
    size_t images_loaded;
    size_t live_graphs;
    size_t live_stacks;
    int exit_code;
    unsigned actual_guest_entry;
    unsigned instruction_trace_available;
    char diagnostic[256];
} ios27_24e_report;

/*
 * The only executable subset is trusted, freestanding compiler-linked ARM64
 * Mach-O with validated LC_MAIN == exported _main and no initializers,
 * Objective-C, TLS, syscalls, or dyld runtime services. The merged loader
 * binds imports and seals W^X mappings; no Apple dyld is executed.
 *
 * This entry bridge does NOT emulate arbitrary Darwin process startup and
 * is NOT a sandbox. On x86_64 it validates metadata but NEVER counts native
 * execution. A failure always reports zero live resources.
 */
int ios27_24e_run(const ios27_24e_application *app, ios27_24e_report *report);

#endif
