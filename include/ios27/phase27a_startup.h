#ifndef IOS27_PHASE27A_STARTUP_H
#define IOS27_PHASE27A_STARTUP_H
#include <stddef.h>
#include <stdint.h>

/* Loader-neutral execution transaction for prevalidated Darwin images.
 * Does not parse or map Mach-O, execute guest instructions, or implement ObjC.
 * All supplied pointers are trusted, host-owned and live for the call duration.
 */
#define IOS27_PHASE27A_MAX_IMAGES 24u
#define IOS27_PHASE27A_MAX_DEPS 16u
#define IOS27_PHASE27A_MAX_ARGV 16u
#define IOS27_PHASE27A_MAX_ENV 32u
#define IOS27_PHASE27A_MAX_APPLE 8u
#define IOS27_PHASE27A_MAX_STRING 256u
#define IOS27_PHASE27A_MAX_INPUT_BYTES 8192u

typedef enum {
    IOS27_PHASE27A_OK = 0,
    IOS27_PHASE27A_INVALID = 1,
    IOS27_PHASE27A_UNSUPPORTED = 2,
    IOS27_PHASE27A_CYCLE = 3,
    IOS27_PHASE27A_ACTIVATION_FAILED = 4,
    IOS27_PHASE27A_ENTRY_FAILED = 5,
    IOS27_PHASE27A_GUEST_FAILED = 6,
    IOS27_PHASE27A_ROLLBACK_FAILED = 7
} ios27_phase27a_status;

typedef struct {
    uint32_t id;                   /* nonzero unique image identity */
    const char *name;              /* bounded, nonempty diagnostic label */
    const uint32_t *dependencies;  /* image IDs, not list indices */
    size_t dependency_count;
    uint64_t text_fileoff;         /* exact verified mapped __TEXT file range */
    uint64_t text_filesize;
    uint64_t entry_fileoff;        /* LC_MAIN entryoff from verified loader */
    unsigned has_main : 1;
    unsigned text_executable : 1;
    unsigned text_writable : 1;
} ios27_phase27a_image;

typedef struct {
    int argc;
    const char *const *argv;
    size_t envc;
    const char *const *env;
    size_t applec;
    const char *const *apple;
} ios27_phase27a_inputs;

typedef struct {
    int argc;
    char *const *argv;
    char *const *env;
    char *const *apple;
} ios27_phase27a_frame;

typedef struct {
    /* No code execution occurs in this library. The owner verifies all
     * metadata, fixes up mappings and safely dispatches through its runtime. */
    int (*activate)(void *ctx, const ios27_phase27a_image *image);
    int (*rollback)(void *ctx, const ios27_phase27a_image *image);
    int (*enter_main)(void *ctx, const ios27_phase27a_image *main_image,
                      const ios27_phase27a_frame *frame, int *exit_code);
} ios27_phase27a_hooks;

typedef struct {
    const ios27_phase27a_image *images;
    size_t image_count;
    ios27_phase27a_inputs inputs;
    ios27_phase27a_hooks hooks;
    void *context;
} ios27_phase27a_request;

typedef struct {
    ios27_phase27a_status status;
    ios27_phase27a_status cause;  /* original error if rollback failed */
    uint32_t failed_image;
    int guest_exit_code;
    size_t activated;
    size_t rolled_back;
    unsigned main_invocations;
} ios27_phase27a_result;

/* Atomic preflight: no activation, callbacks or mutations on invalid inputs.
 * Any activation/entry failure rolls back activated images in reverse order.
 * Successful exit also tears down images in reverse dependency order.
 * Does not imply that an ARM64 Mach-O was natively executed: enter_main
 * must be wired to a separately verified loader/guest entry ABI. */
ios27_phase27a_status ios27_phase27a_launch(const ios27_phase27a_request *request,
                                            ios27_phase27a_result *out);
#endif
