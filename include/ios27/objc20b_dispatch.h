#ifndef IOS27_OBJC20B_DISPATCH_H
#define IOS27_OBJC20B_DISPATCH_H
#include <stddef.h>
#include <stdint.h>
/* Native pointer-identity registry. Single-threaded; caller owns executable IMP
 * mappings and ensures signatures and lifetimes. No executable-byte emulation. */
#define IOS27_OBJC20B_CLASSES 96u
#define IOS27_OBJC20B_SELECTORS 512u
#define IOS27_OBJC20B_METHODS 64u
#define IOS27_OBJC20B_OBJECTS 1024u
#define IOS27_OBJC20B_NAME 96u
#define IOS27_OBJC20B_CACHE 512u
typedef struct ios27_objc20b_runtime ios27_objc20b_runtime;
typedef const void *ios27_objc20b_sel;
typedef uintptr_t (*ios27_objc20b_imp)(void *, ios27_objc20b_sel, ...);
typedef int (*ios27_objc20b_initializer)(void *context, void *class_object);
typedef struct {
    const char *name;
    ios27_objc20b_imp imp;
    uint8_t word_arguments; /* 0..4: uintptr_t arguments and return */
} ios27_objc20b_method;
typedef struct {
    const char *name;
    const char *superclass; /* NULL for root class */
    void *class_object;
    void *metaclass_object;
    size_t instance_size;
    const ios27_objc20b_method *instances;
    size_t instance_count;
    const ios27_objc20b_method *classes;
    size_t class_count;
    ios27_objc20b_initializer initialize;
    void *context;
} ios27_objc20b_class;
typedef struct {
    uint64_t cache_hits, cache_misses, epoch;
    size_t classes, selectors, objects;
} ios27_objc20b_stats;
int ios27_objc20b_create(ios27_objc20b_runtime **out);
void ios27_objc20b_destroy(ios27_objc20b_runtime *runtime);
/* Entire batch is atomic; parents may be listed after children. */
int ios27_objc20b_register(ios27_objc20b_runtime *runtime,
                          const ios27_objc20b_class *classes, size_t count);
int ios27_objc20b_selector(ios27_objc20b_runtime *runtime,
                          const char *name, ios27_objc20b_sel *out);
void *ios27_objc20b_alloc(ios27_objc20b_runtime *runtime, const char *class_name);
int ios27_objc20b_bind(ios27_objc20b_runtime *runtime, void *object,
                      const char *class_name);
int ios27_objc20b_release(ios27_objc20b_runtime *runtime, void *object);
/* Lookup validates receiver identity without dereferencing arbitrary addresses.
 * super_current is the lexically enclosing class name, never the parent name. */
int ios27_objc20b_resolve(ios27_objc20b_runtime *runtime, const void *receiver,
                         ios27_objc20b_sel selector, const char *super_current,
                         ios27_objc20b_imp *out, uint8_t *word_arguments);
int ios27_objc20b_send_words(ios27_objc20b_runtime *runtime, void *receiver,
                            ios27_objc20b_sel selector, const char *super_current,
                            const uintptr_t *args, size_t argc, uintptr_t *result);
int ios27_objc20b_replace(ios27_objc20b_runtime *runtime, const char *class_name,
                         int class_method, const ios27_objc20b_method *method);
void ios27_objc20b_statistics(const ios27_objc20b_runtime *runtime,
                              ios27_objc20b_stats *out);
/* Explicit process-wide runtime for native AArch64 entry stubs. */
int ios27_objc20b_set_global(ios27_objc20b_runtime *runtime);
void *ios27_objc20b_resolve_global(void *receiver, ios27_objc20b_sel selector);
void *ios27_objc20b_resolve_super_global(const void *objc_super,
                                         ios27_objc20b_sel selector);
#endif
