#ifndef IOS27_PHASE23E_HARNESS_H
#define IOS27_PHASE23E_HARNESS_H
#include <stddef.h>
#include <stdint.h>
#include "ios27/images.h"

#define IOS27_23E_MAX_BYTES (4u * 1024u * 1024u)
#define IOS27_23E_MAX_ARGS 32u
#define IOS27_23E_MAX_STRING 1024u

typedef enum {
    IOS27_23E_OK = 0,
    IOS27_23E_REJECTED = 1,
    IOS27_23E_UNSUPPORTED_ABI = 2,
    IOS27_23E_UNSUPPORTED_HOST = 3,
    IOS27_23E_LOAD_FAILED = 4,
    IOS27_23E_EXECUTION_FAILED = 5
} ios27_23e_status;

typedef struct {
    const char *name;
    const char *guest_path;
    const uint8_t *data;
    size_t size;
} ios27_23e_image;

typedef struct {
    const ios27_23e_image *images;
    size_t count;
    const char *main_name;
    const char *main_path;
    int argc;
    const char *const *argv;
    const char *const *envp;
    const char *const *apple;
    unsigned needs_objc;
    unsigned needs_mach;
    unsigned needs_threads;
} ios27_23e_application;

typedef struct {
    ios27_23e_status status;
    uint64_t trace;
    uint64_t verified_guest_operations;
    size_t images_loaded;
    size_t mapped_graphs_live;
    int exit_code;
    char diagnostic[256];
} ios27_23e_report;

/* All images are trusted original-source compiler fixtures, not sandboxed.
 * A successful preflight is NOT a claim that Darwin application startup works.
 * No guest execution occurs if the existing loader denies the entrypoint. */
int ios27_23e_application_probe(const ios27_23e_application *app,
                                 ios27_23e_report *report);

/* Direct calls only to verified, freestanding exports of explicit MH_DYLIBs.
 * The loop checks every returned value, not a synthetic success counter.
 * This is NOT Darwin MH_EXECUTE startup or a surrogate for thread/Mach APIs. */
int ios27_23e_library_stress(const ios27_23e_image *images, size_t count,
                             const char *root_image, unsigned iterations,
                             ios27_23e_report *report);
#endif
