#ifndef IOS27_PHASE25B_DISPATCH_H
#define IOS27_PHASE25B_DISPATCH_H
#include <stddef.h>
#include <stdint.h>

/* Experimental restricted AArch64 PCS, not Apple's libobjc. No sret, PAC,
 * variadics, forwarding, exceptions, aggregates, blocks or mixed FP classes. */
#define PHASE25B_MAX_CLASSES 32u
#define PHASE25B_MAX_IMAGES 24u
#define PHASE25B_MAX_SELECTORS 128u
#define PHASE25B_MAX_OBJECTS 256u
#define PHASE25B_MAX_METHODS 32u
#define PHASE25B_NAME_LENGTH 80u

typedef struct phase25b_runtime phase25b_runtime;
typedef const void *phase25b_sel;
typedef struct {
    void *receiver;
    void *current_class; /* lexical class (or metaclass) owning the super send */
} phase25b_super;
typedef enum {
    PHASE25B_UNSUPPORTED = -1,
    PHASE25B_VOID0 = 0, PHASE25B_WORD0, PHASE25B_WORD1,
    PHASE25B_WORD2, PHASE25B_WORD3, PHASE25B_WORD4,
    PHASE25B_WORD5, PHASE25B_WORD6,
    PHASE25B_FLOAT1, PHASE25B_DOUBLE1
} phase25b_abi;
typedef enum {
    PHASE25B_OK = 0, PHASE25B_NIL = 1, PHASE25B_BAD_RECEIVER = 2,
    PHASE25B_BAD_SELECTOR = 3, PHASE25B_BAD_SUPER = 4,
    PHASE25B_BAD_METHOD = 5, PHASE25B_RETIRED = 6
} phase25b_status;
typedef struct {
    uint64_t hits, misses, epoch, sends, rejects, nil_sends;
    size_t classes, selectors, images, live_objects, in_flight;
} phase25b_stats;

int phase25b_create(phase25b_runtime **out);
/* Destruction requires external quiescence: no concurrent API calls. */
int phase25b_destroy(phase25b_runtime *runtime);
/* Copy verified relocation-free ARM64 bytes, then mprotect RW -> RX.
 * offset/length are image-local; ownership never escapes the runtime. */
int phase25b_publish_image(phase25b_runtime *runtime, const void *bytes,
                           size_t length, uint32_t *out_image);
int phase25b_retire_image(phase25b_runtime *runtime, uint32_t image);
int phase25b_register_class(phase25b_runtime *runtime, const char *name,
                            const char *parent);
int phase25b_class_token(phase25b_runtime *runtime, const char *name,
                         int metaclass, void **out);
int phase25b_selector(phase25b_runtime *runtime, const char *name,
                      phase25b_sel *out);
/* replace=0: reject duplicate; replace=1: existing selector required.
 * A category applies methods using replace=1, in dependency installation order. */
int phase25b_method(phase25b_runtime *runtime, const char *class_name,
                    int class_method, const char *selector, const char *encoding,
                    uint32_t image, size_t offset, int replace);
void *phase25b_alloc(phase25b_runtime *runtime, const char *class_name);
/* returns -2 while the receiver is pinned by a send. */
int phase25b_release(phase25b_runtime *runtime, void *receiver);
phase25b_abi phase25b_parse_encoding(const char *encoding);
void phase25b_statistics(phase25b_runtime *runtime, phase25b_stats *out);
phase25b_status phase25b_last_status(void);
/* Registers the process-wide entrypoint. Switching runtimes requires zero
 * in-flight invocations; no concurrent destruction allowed. */
int phase25b_set_active(phase25b_runtime *runtime);

/* AArch64 assembly preserves x0-x7, v0-v7, x8 and 16-byte SP alignment,
 * dispatches a verified method and retains its receiver and image pins until
 * the callee returns. Arguments/returns must match phase25b_parse_encoding. */
uintptr_t phase25b_objc_msgSend(void *receiver, phase25b_sel selector, ...);
uintptr_t phase25b_objc_msgSendSuper2(const phase25b_super *super,
                                     phase25b_sel selector, ...);
/* Benchmark-only direct verified-IMP call. Caller holds a pin on `code`.
 * AArch64 implementation calls the exact executable bytes without resolving. */
uintptr_t phase25b_direct_word1(void *receiver, phase25b_sel selector,
                                uintptr_t value, const void *code);
/* The following are strictly private assembly helper ABI, not user APIs. */
typedef struct {
    void *receiver;
    void *runtime;
    uint32_t object_slot_plus_one;
    uint32_t image_slot_plus_one;
} phase25b_ticket;
void *phase25b_prepare(const void *receiver, phase25b_sel selector,
                       phase25b_ticket *ticket, int is_super);
void phase25b_finish(phase25b_ticket *ticket);
#endif
