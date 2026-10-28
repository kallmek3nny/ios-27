#include "ios27/objc_runtime.h"

#include <stdlib.h>
#include <string.h>

#define INVALID_INDEX UINT16_MAX
#define MAX_INSTANCE 4096u

typedef struct {
    uint16_t selector;
    ios27_objc_imp implementation;
} method_entry;
typedef struct {
    char name[IOS27_OBJC_NAME_LIMIT];
    uint16_t parent;
    uint8_t initialization; /* 0 not started, 1 in progress, 2 complete */
    size_t size;
    ios27_objc_initializer initializer;
    size_t method_count;
    method_entry methods[IOS27_OBJC_MAX_METHODS];
} class_entry;
typedef struct {
    void *address;
    uint16_t class_index;
} object_entry;
struct ios27_objc_runtime {
    size_t class_count, selector_count, object_count;
    class_entry classes[IOS27_OBJC_MAX_CLASSES];
    char selectors[IOS27_OBJC_MAX_SELECTORS][IOS27_OBJC_NAME_LIMIT];
    object_entry objects[IOS27_OBJC_MAX_OBJECTS];
};
static int valid_name(const char *s) {
    size_t n;
    if (!s) return 0;
    for (n = 0u; n < IOS27_OBJC_NAME_LIMIT; ++n) {
        unsigned char c = (unsigned char)s[n];
        if (!c) return n > 0u;
        if (c < 33u || c > 126u) return 0;
    }
    return 0;
}
static int class_id(const ios27_objc_runtime *r, const char *name) {
    if (!r || !valid_name(name)) return -1;
    for (size_t i = 0u; i < r->class_count; ++i)
        if (strcmp(r->classes[i].name, name) == 0) return (int)i;
    return -1;
}
static int selector_id(const ios27_objc_runtime *r, const char *name) {
    if (!r || !valid_name(name)) return -1;
    for (size_t i = 0u; i < r->selector_count; ++i)
        if (strcmp(r->selectors[i], name) == 0) return (int)i;
    return -1;
}
static int intern(ios27_objc_runtime *r, const char *name) {
    int id = selector_id(r, name);
    if (id >= 0) return id;
    if (!valid_name(name) || r->selector_count >= IOS27_OBJC_MAX_SELECTORS)
        return -1;
    id = (int)r->selector_count++;
    (void)strcpy(r->selectors[id], name);
    return id;
}
int ios27_objc_runtime_create(ios27_objc_runtime **out) {
    if (!out) return -1;
    *out = calloc(1u, sizeof(**out));
    return *out ? 0 : -1;
}
void ios27_objc_runtime_destroy(ios27_objc_runtime *r) {
    if (!r) return;
    for (size_t i = 0; i < r->object_count; ++i) free(r->objects[i].address);
    free(r);
}
int ios27_objc_class_index(const ios27_objc_runtime *r,
                           const char *name, size_t *index) {
    int id = class_id(r, name);
    if (id < 0 || !index) return -1;
    *index = (size_t)id;
    return 0;
}
int ios27_objc_selector(ios27_objc_runtime *r,
                        const char *name, uint16_t *token) {
    int id;
    if (!r || !token) return -1;
    id = intern(r, name);
    if (id < 0) return -1;
    *token = (uint16_t)id;
    return 0;
}
/* Class registration is prepared in a heap snapshot; no partial names,
 * selector table entries, or class state survive a failed transaction. */
int ios27_objc_register_classes(ios27_objc_runtime *r,
                                const ios27_objc_class_spec *specs,
                                size_t count) {
    ios27_objc_runtime *stage;
    int success = -1;
    if (!r || !specs || !count || count > IOS27_OBJC_MAX_CLASSES - r->class_count)
        return -1;
    stage = malloc(sizeof(*stage));
    if (!stage) return -1;
    memcpy(stage, r, sizeof(*stage));
    size_t begin = stage->class_count;
    for (size_t i = 0; i < count; ++i) {
        const ios27_objc_class_spec *s = &specs[i];
        class_entry *c = &stage->classes[begin + i];
        if (!valid_name(s->name) || class_id(stage, s->name) >= 0 ||
            s->instance_size < sizeof(void *) || s->instance_size > MAX_INSTANCE ||
            s->method_count > IOS27_OBJC_MAX_METHODS ||
            (s->method_count && !s->methods) ||
            (s->superclass && !valid_name(s->superclass))) goto finish;
        (void)strcpy(c->name, s->name);
        c->size = s->instance_size;
        c->initializer = s->initialize;
        c->parent = INVALID_INDEX;
        c->method_count = s->method_count;
        for (size_t m = 0; m < s->method_count; ++m) {
            int token;
            if (!s->methods[m].implementation ||
                (token = intern(stage, s->methods[m].selector)) < 0)
                goto finish;
            for (size_t k = 0; k < m; ++k)
                if (c->methods[k].selector == (uint16_t)token) goto finish;
            c->methods[m].selector = (uint16_t)token;
            c->methods[m].implementation = s->methods[m].implementation;
        }
        ++stage->class_count;
    }
    for (size_t i = 0; i < count; ++i) {
        const char *parent = specs[i].superclass;
        class_entry *c = &stage->classes[begin + i];
        int id;
        if (!parent) continue;
        id = class_id(stage, parent);
        if (id < 0 || id == (int)(begin + i)) goto finish;
        c->parent = (uint16_t)id;
        if (stage->classes[id].size > c->size) goto finish;
    }
    for (size_t i = begin; i < stage->class_count; ++i) {
        uint16_t p = (uint16_t)i;
        size_t steps = 0;
        while (p != INVALID_INDEX) {
            if (++steps > stage->class_count) goto finish;
            p = stage->classes[p].parent;
        }
    }
    memcpy(r, stage, sizeof(*r));
    success = 0;
finish:
    free(stage);
    return success;
}
static int initialize(ios27_objc_runtime *r, uint16_t index) {
    class_entry *c = &r->classes[index];
    if (c->initialization == 2u) return 0;
    if (c->initialization == 1u) return -1;
    c->initialization = 1u;
    if ((c->parent != INVALID_INDEX && initialize(r, c->parent)) ||
        (c->initializer && c->initializer())) {
        c->initialization = 0u;
        return -1;
    }
    c->initialization = 2u;
    return 0;
}
void *ios27_objc_alloc(ios27_objc_runtime *r, const char *name) {
    int id = class_id(r, name);
    void *p;
    if (id < 0 || r->object_count >= IOS27_OBJC_MAX_OBJECTS ||
        initialize(r, (uint16_t)id)) return NULL;
    p = calloc(1u, r->classes[id].size);
    if (!p) return NULL;
    /* Canonical native isa ABI is deferred to the image loader: this host
     * runtime records object identity out of band, not a forged guest isa. */
    r->objects[r->object_count].address = p;
    r->objects[r->object_count].class_index = (uint16_t)id;
    ++r->object_count;
    return p;
}
static int object_index(const ios27_objc_runtime *r, const void *address) {
    if (!r || !address) return -1;
    for (size_t i = 0; i < r->object_count; ++i)
        if (r->objects[i].address == address) return (int)i;
    return -1;
}
int ios27_objc_release(ios27_objc_runtime *r, void *address) {
    int id = object_index(r, address);
    if (id < 0) return -1;
    free(r->objects[id].address);
    --r->object_count;
    r->objects[id] = r->objects[r->object_count];
    r->objects[r->object_count].address = NULL;
    return 0;
}
static int lookup(const ios27_objc_runtime *r, uint16_t class_index,
                  int selector, ios27_objc_imp *imp) {
    size_t steps = 0;
    while (class_index != INVALID_INDEX && steps++ < r->class_count) {
        const class_entry *c = &r->classes[class_index];
        for (size_t i = 0; i < c->method_count; ++i) {
            if (c->methods[i].selector == (uint16_t)selector) {
                *imp = c->methods[i].implementation;
                return 0;
            }
        }
        class_index = c->parent;
    }
    return -1;
}
int ios27_objc_lookup(const ios27_objc_runtime *r, const char *name,
                      const char *selector, ios27_objc_imp *implementation) {
    int id = class_id(r, name);
    int sel = selector_id(r, selector);
    if (!implementation || id < 0 || sel < 0) return -1;
    *implementation = NULL;
    return lookup(r, (uint16_t)id, sel, implementation);
}
int ios27_objc_send(ios27_objc_runtime *r, const void *object,
                    const char *selector, intptr_t *result) {
    int id, sel;
    ios27_objc_imp impl = NULL;
    if (!result || (id = object_index(r, object)) < 0 ||
        (sel = selector_id(r, selector)) < 0 ||
        lookup(r, r->objects[id].class_index, sel, &impl)) return -1;
    *result = impl((void *)object, r->selectors[sel]);
    return 0;
}
void ios27_objc_counts(const ios27_objc_runtime *r,
                       size_t *classes, size_t *selectors, size_t *objects) {
    if (classes) *classes = r ? r->class_count : 0u;
    if (selectors) *selectors = r ? r->selector_count : 0u;
    if (objects) *objects = r ? r->object_count : 0u;
}
