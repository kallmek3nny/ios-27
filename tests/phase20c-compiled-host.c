/* Minimal *test-only* GNU Objective-C legacy selector lookup. Not a Darwin
 * objc_msgSend replacement. Compiled methods and sends are real Clang code. */
#include "ios27/objc20c_lifetime.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*generic_imp)(void);
typedef struct {
    const char *name;
    const char *types;
    generic_imp implementation;
} objc_method;
typedef struct objc_method_list {
    struct objc_method_list *next;
    int count;
    objc_method entries[];
} objc_method_list;
typedef struct objc_class {
    struct objc_class *isa;
    struct objc_class *superclass;
    const char *name;
    long version;
    long info;
    long size;
    void *ivars;
    objc_method_list *methods;
} objc_class;
typedef struct { objc_class *isa; long value; void *child; } native_object;

extern objc_class _OBJC_CLASS_Ledger;
extern objc_class _OBJC_CLASS_Extended;
extern int objc_value_offset __asm__("__objc_ivar_offset_value_Ledger.value");
extern int objc_child_offset __asm__("__objc_ivar_offset_value_Ledger.child");
extern long phase20c_compiled_graph(void *, void *);

static ios27_objc20c_runtime *runtime;
static unsigned deallocated;
static uint64_t pool_token;

void __objc_exec_class(void *module) {
    /* Clang GNU-legacy constructor supplies the complete registration
     * module. This test only needs fixed class/ivar identities. */
    assert(module != NULL);
    objc_value_offset = (int)offsetof(native_object, value);
    objc_child_offset = (int)offsetof(native_object, child);
}

generic_imp objc_msg_lookup(void *receiver, void *selector) {
    const char *name;
    objc_class *cls;
    if (!receiver || !selector) abort();
    name = *(const char *const *)selector;
    cls = *(objc_class **)receiver;
    for (unsigned depth = 0; cls && depth < 3u; ++depth) {
        objc_method_list *methods = cls->methods;
        for (unsigned j = 0; methods && j < 4u; ++j, methods = methods->next)
            for (int i = 0; i < methods->count; ++i)
                if (strcmp(methods->entries[i].name, name) == 0)
                    return methods->entries[i].implementation;
        cls = cls == &_OBJC_CLASS_Extended ? &_OBJC_CLASS_Ledger : NULL;
    }
    fprintf(stderr, "unknown selector: %s\n", name);
    abort();
}

int phase20c_store_child(void *owner, void *child) {
    return ios27_objc20c_strong_store(runtime,
                                     &((native_object *)owner)->child, child);
}
int phase20c_queue(void *object) {
    return ios27_objc20c_autorelease(runtime, object);
}

static void destroy(ios27_objc20c_runtime *rt, void *object, void *ctx) {
    native_object *n = object;
    (void)ctx;
    ++deallocated;
    assert(ios27_objc20c_strong_store(rt, &n->child, NULL) == 0);
}
static native_object *make(objc_class *isa, long value) {
    native_object *obj = NULL;
    assert(ios27_objc20c_alloc(runtime, sizeof(*obj), destroy, NULL,
                              (void **)&obj) == 0);
    obj->isa = isa;
    obj->value = value;
    return obj;
}

int main(void) {
    assert(ios27_objc20c_create(&runtime) == 0);
    assert(strcmp(_OBJC_CLASS_Ledger.name, "Ledger") == 0);
    assert(strcmp(_OBJC_CLASS_Extended.name, "Extended") == 0);
    assert(_OBJC_CLASS_Ledger.methods->count == 4);
    assert(_OBJC_CLASS_Extended.methods->count == 1);
    assert(objc_value_offset == (int)offsetof(native_object, value));
    assert(objc_child_offset == (int)offsetof(native_object, child));
    for (int i = 0; i < 512; ++i) {
        native_object *root = make(&_OBJC_CLASS_Ledger, 7);
        native_object *derived = make(&_OBJC_CLASS_Extended, 20);
        /* Inherited recursive compiled calls + overriden base + compiled
         * setter backed by our managed strong reference implementation. */
        long actual = phase20c_compiled_graph(root, derived);
        if (actual != 42) fprintf(stderr, "graph=%ld root=%ld derived=%ld offsets=%d/%d\n",actual,root->value,derived->value,objc_value_offset,objc_child_offset);
        assert(actual == 42);
        assert(root->child == derived);
        assert(ios27_objc20c_pool_push(runtime, &pool_token) == 0);
        assert(ios27_objc20c_retain(runtime, root) == 0);
        /* Test-only dynamic invocation of a genuine Clang method body. */
        typedef void (*queue_imp)(void *, void *);
        const char *raw_sel[2] = {"queue", "v16@0:8"};
        queue_imp queue = (queue_imp)objc_msg_lookup(root, raw_sel);
        queue(root, (void *)raw_sel);
        assert(ios27_objc20c_pool_pop(runtime, pool_token) == 0);
        assert(ios27_objc20c_release(runtime, root) == 0);
        assert(ios27_objc20c_release(runtime, derived) == 0);
    }
    ios27_objc20c_stats stats;
    ios27_objc20c_get_stats(runtime, &stats);
    assert(stats.live_objects == 0 && deallocated == 1024u);
    assert(ios27_objc20c_validate(runtime) == 0);
    ios27_objc20c_destroy(runtime);
    puts("phase20c: real clang objc methods + recursive dispatch + ownership ok");
    return 0;
}
