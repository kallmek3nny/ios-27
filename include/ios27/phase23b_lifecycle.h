#ifndef IOS27_PHASE23B_LIFECYCLE_H
#define IOS27_PHASE23B_LIFECYCLE_H
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#define IOS27_23B_MAX_IMAGES 16u
#define IOS27_23B_MAX_DEPS 8u
#define IOS27_23B_MAX_CALLBACKS 64u
#define IOS27_23B_MAX_SEGMENTS 24u
#define IOS27_23B_MAX_REFS 65535u

typedef struct { uint64_t start, end; } ios27_23b_exec_range;
typedef struct {
    uint64_t init_table, term_table;
    size_t init_count, term_count, exec_count;
    uint64_t init[IOS27_23B_MAX_CALLBACKS];
    uint64_t term[IOS27_23B_MAX_CALLBACKS];
    ios27_23b_exec_range exec[IOS27_23B_MAX_SEGMENTS];
} ios27_23b_image;

typedef int (*ios27_23b_read_ptr)(void *context, uint64_t image_vmaddr,
                                  uint64_t *value);
/* All callbacks are delegated to a trusted caller. The library never jumps
   to an address supplied by an image. ROLLBACK is an explicit compensator,
   not an implicit Mach-O destructor. */
typedef enum { IOS27_23B_CONSTRUCT, IOS27_23B_DESTRUCT,
               IOS27_23B_ROLLBACK } ios27_23b_action;
typedef int (*ios27_23b_dispatch)(void *context, unsigned image,
                                  ios27_23b_action action, uint64_t target,
                                  size_t ordinal);
typedef struct { unsigned index; uint32_t generation; } ios27_23b_handle;
typedef enum { IOS27_23B_NEW, IOS27_23B_STARTING, IOS27_23B_READY,
               IOS27_23B_FAILED, IOS27_23B_STOPPING } ios27_23b_state;
typedef struct {
    ios27_23b_image image;
    unsigned deps[IOS27_23B_MAX_DEPS];
    size_t dep_count;
    unsigned refs, active;
    uint32_t generation;
    pthread_t owner;
    ios27_23b_state state;
    int failure;
    int registered;
} ios27_23b_slot;
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    ios27_23b_slot slot[IOS27_23B_MAX_IMAGES];
    size_t count;
    unsigned initialized;
} ios27_23b_runtime;

/* Examine bounded compiler-produced ARM64 MH_DYLIB sections. Neither this
   function nor bind executes guest code. */
int ios27_23b_parse(const uint8_t *file, size_t length,
                    ios27_23b_image *out);
/* Resolve callback slots AFTER the caller has verified fixups and mappings.
   Reader returns preferred/unslid VM values; fail closed on foreign, W+X,
   unaligned, zero, or non-executable targets. On failure output is unchanged. */
int ios27_23b_bind(ios27_23b_image *image, ios27_23b_read_ptr read_ptr,
                   void *context);
int ios27_23b_runtime_init(ios27_23b_runtime *runtime);
int ios27_23b_add(ios27_23b_runtime *runtime, unsigned index,
                  const ios27_23b_image *image, const unsigned *deps,
                  size_t dep_count);
int ios27_23b_open(ios27_23b_runtime *runtime, unsigned index,
                   ios27_23b_handle *out);
/* 0 = ready. Failure is sticky until the last reference is closed. */
int ios27_23b_ensure(ios27_23b_runtime *runtime, ios27_23b_handle handle,
                     ios27_23b_dispatch dispatch, void *context);
int ios27_23b_close(ios27_23b_runtime *runtime, ios27_23b_handle handle,
                    ios27_23b_dispatch dispatch, void *context);
int ios27_23b_snapshot(ios27_23b_runtime *runtime, unsigned index,
                       ios27_23b_state *state, unsigned *refs,
                       unsigned *active);
/* EBUSY if any image is still acquired or in a callback. */
int ios27_23b_runtime_destroy(ios27_23b_runtime *runtime);
#endif
