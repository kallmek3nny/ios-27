#include "ios27/objc_bridge.h"
#include <stdlib.h>
#include <string.h>

int ios27_objc_register_metadata(ios27_objc_runtime *runtime,
                                 const ios27_objc_metadata *metadata,
                                 ios27_objc_resolve_host_imp resolver,
                                 void *context) {
    ios27_objc_class_spec classes[IOS27_OBJC_METADATA_CLASSES];
    ios27_objc_method_spec *methods;
    int result = -1;
    size_t total = 0u, at = 0u;
    if (!runtime || !metadata || !resolver || !metadata->class_count ||
        metadata->class_count > IOS27_OBJC_METADATA_CLASSES) return -1;
    for (size_t i = 0; i < metadata->class_count; ++i) {
        if (metadata->classes[i].method_count > IOS27_OBJC_METADATA_METHODS ||
            total > IOS27_OBJC_METADATA_CLASSES * IOS27_OBJC_METADATA_METHODS -
                    metadata->classes[i].method_count) return -1;
        total += metadata->classes[i].method_count;
    }
    methods = calloc(total ? total : 1u, sizeof(*methods));
    if (!methods) return -1;
    memset(classes, 0, sizeof(classes));
    for (size_t i = 0; i < metadata->class_count; ++i) {
        const ios27_objc_metadata_class *c = &metadata->classes[i];
        classes[i].name = c->name;
        classes[i].superclass = c->superclass[0] ? c->superclass : NULL;
        classes[i].instance_size = c->instance_size;
        classes[i].method_count = c->method_count;
        classes[i].methods = methods + at;
        for (size_t j = 0; j < c->method_count; ++j) {
            methods[at].selector = c->methods[j].selector;
            methods[at].implementation = resolver(context, c->name,
                                                  c->methods[j].selector);
            if (!methods[at].implementation) goto finish;
            ++at;
        }
    }
    result = ios27_objc_register_classes(runtime, classes,
                                         metadata->class_count);
finish:
    free(methods);
    return result;
}
