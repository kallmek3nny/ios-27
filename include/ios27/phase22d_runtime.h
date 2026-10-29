#ifndef IOS27_PHASE22D_RUNTIME_H
#define IOS27_PHASE22D_RUNTIME_H
#include <stddef.h>
#include <stdint.h>

/* Phase 22d: isolated, bounded ObjC-compatible runtime substrate.
 * Not a drop-in Apple libobjc, ARC, exception, Swift or arm64e implementation.
 * All IMPs must be trusted, correctly mapped host/native function pointers.
 * Users must quiesce all threads and unregister weak locations before destroy.
 */
#define PHASE22D_CLASSES 64u
#define PHASE22D_SELECTORS 256u
#define PHASE22D_METHODS 32u
#define PHASE22D_OBJECTS 32768u
#define PHASE22D_LIVE_LIMIT 4096u
#define PHASE22D_WEAK_LIMIT 8192u
#define PHASE22D_MAX_OBJECT_SIZE 4096u
#define PHASE22D_MEMORY_LIMIT (16u * 1024u * 1024u)
#define PHASE22D_NAME 80u

typedef struct phase22d_runtime phase22d_runtime;
typedef const void *phase22d_sel;
typedef uintptr_t (*phase22d_word0)(void *, phase22d_sel);
typedef uintptr_t (*phase22d_word1)(void *, phase22d_sel, uintptr_t);
typedef uintptr_t (*phase22d_word2)(void *, phase22d_sel, uintptr_t, uintptr_t);
typedef uintptr_t (*phase22d_word3)(void *, phase22d_sel, uintptr_t, uintptr_t, uintptr_t);
typedef uintptr_t (*phase22d_word4)(void *, phase22d_sel, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
typedef double (*phase22d_double1)(void *, phase22d_sel, double);
typedef int (*phase22d_initializer)(void *, void *);
typedef void (*phase22d_deallocator)(void *, void *);

typedef enum {
    PHASE22D_WORD0 = 1, PHASE22D_WORD1, PHASE22D_WORD2,
    PHASE22D_WORD3, PHASE22D_WORD4, PHASE22D_DOUBLE1
} phase22d_signature;
typedef union {
    phase22d_word0 word0;
    phase22d_word1 word1;
    phase22d_word2 word2;
    phase22d_word3 word3;
    phase22d_word4 word4;
    phase22d_double1 double1;
} phase22d_implementation;
typedef struct {
    const char *selector;
    phase22d_signature signature;
    phase22d_implementation implementation;
} phase22d_method_spec;
typedef struct {
    const char *name;
    const char *superclass;
    size_t instance_size;
    const phase22d_method_spec *instance_methods;
    size_t instance_count;
    const phase22d_method_spec *class_methods;
    size_t class_count;
    phase22d_initializer initialize;
    void *context;
    phase22d_deallocator deallocate;
} phase22d_class_spec;
typedef struct {
    const char *class_name;
    int class_method;
    const phase22d_method_spec *methods;
    size_t count;
} phase22d_category;
typedef struct {
    uint64_t sends, failed_sends, initializations, init_waits;
    uint64_t allocations, releases, destroyed, categories;
    size_t classes, selectors, lifetime_objects, live_objects;
    size_t retained_bytes, weak_locations;
} phase22d_stats;

int phase22d_create(phase22d_runtime **out);
void phase22d_destroy(phase22d_runtime *runtime);
int phase22d_register(phase22d_runtime *, const phase22d_class_spec *, size_t count);
int phase22d_install_category(phase22d_runtime *, const phase22d_category *);
int phase22d_selector(phase22d_runtime *, const char *name, phase22d_sel *out);
int phase22d_class_object(phase22d_runtime *, const char *name, void **out);
int phase22d_metaclass_object(phase22d_runtime *, const char *name, void **out);
int phase22d_allocate(phase22d_runtime *, const char *name, void **out);
int phase22d_retain(phase22d_runtime *, const void *object);
int phase22d_release(phase22d_runtime *, const void *object);
int phase22d_weak_store(phase22d_runtime *, void **slot, void *object);
int phase22d_weak_load_retained(phase22d_runtime *, void **slot, void **out);
int phase22d_weak_unregister(phase22d_runtime *, void **slot);
/* 'lexical_super' is the lexical class containing a [super ...] send,
 * not the receiver's dynamic class. NULL means regular dispatch. */
int phase22d_send_words(phase22d_runtime *, void *receiver, phase22d_sel,
                        const char *lexical_super, const uintptr_t *args,
                        size_t nargs, uintptr_t *out);
int phase22d_send_double1(phase22d_runtime *, void *receiver, phase22d_sel,
                          const char *lexical_super, double arg, double *out);
int phase22d_statistics(phase22d_runtime *, phase22d_stats *out);
int phase22d_validate(phase22d_runtime *);
#endif
