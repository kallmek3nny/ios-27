#ifndef IOS27_OBJC_RUNTIME_H
#define IOS27_OBJC_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

/* Host-side, bounded ObjC dispatch foundation. Single-threaded; callers
 * supply original, trusted native implementations, never untrusted IMPs. */
#define IOS27_OBJC_MAX_CLASSES 64u
#define IOS27_OBJC_MAX_SELECTORS 256u
#define IOS27_OBJC_MAX_METHODS 32u
#define IOS27_OBJC_MAX_OBJECTS 256u
#define IOS27_OBJC_NAME_LIMIT 64u

typedef struct ios27_objc_runtime ios27_objc_runtime;
typedef intptr_t (*ios27_objc_imp)(void *receiver, const char *selector);
typedef int (*ios27_objc_initializer)(void);
typedef struct {
    const char *selector;
    ios27_objc_imp implementation;
} ios27_objc_method_spec;
typedef struct {
    const char *name;
    const char *superclass; /* NULL only for an ObjC root class */
    size_t instance_size; /* includes the initial isa-sized field */
    ios27_objc_initializer initialize;
    const ios27_objc_method_spec *methods;
    size_t method_count;
} ios27_objc_class_spec;

int ios27_objc_runtime_create(ios27_objc_runtime **out);
void ios27_objc_runtime_destroy(ios27_objc_runtime *runtime);
/* Transactional batch import, including forward-listed parents and cycles. */
int ios27_objc_register_classes(ios27_objc_runtime *runtime,
                                const ios27_objc_class_spec *classes,
                                size_t count);
int ios27_objc_class_index(const ios27_objc_runtime *runtime,
                           const char *name, size_t *index);
int ios27_objc_selector(ios27_objc_runtime *runtime,
                        const char *name, uint16_t *token);
/* Initializes parent then child once. Failed initializers may be retried. */
void *ios27_objc_alloc(ios27_objc_runtime *runtime, const char *class_name);
int ios27_objc_release(ios27_objc_runtime *runtime, void *object);
/* Calling an unknown method fails closed. No dynamic forwarding yet. */
int ios27_objc_send(ios27_objc_runtime *runtime, const void *object,
                    const char *selector, intptr_t *result);
int ios27_objc_lookup(const ios27_objc_runtime *runtime, const char *class_name,
                      const char *selector, ios27_objc_imp *implementation);
void ios27_objc_counts(const ios27_objc_runtime *runtime,
                       size_t *classes, size_t *selectors, size_t *objects);
#endif
