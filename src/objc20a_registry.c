#include "ios27/objc20a_metadata.h"
#include <stdlib.h>
#include <string.h>

#define REG_CLASSES 64u
#define REG_PROTOCOLS 64u
#define REG_CATEGORIES 64u
#define REG_SELECTORS 256u
struct ios27_objc20a_registry {
    uint32_t classes, protocols, categories, selectors;
    ios27_objc20a_class class_items[REG_CLASSES];
    ios27_objc20a_protocol protocol_items[REG_PROTOCOLS];
    ios27_objc20a_category category_items[REG_CATEGORIES];
    char selector_items[REG_SELECTORS][IOS27_OBJC20A_NAME];
};
static int valid(const char *s) {
    size_t i;
    for (i = 0; i < IOS27_OBJC20A_NAME && s[i]; ++i)
        if ((unsigned char)s[i] < 32 || (unsigned char)s[i] > 126) return 0;
    return i != 0 && i < IOS27_OBJC20A_NAME;
}
static int class_index(const ios27_objc20a_registry *r, const char *s) {
    for (unsigned i = 0; i < r->classes; ++i)
        if (!strcmp(r->class_items[i].name, s)) return (int)i;
    return -1;
}
static int protocol_index(const ios27_objc20a_registry *r, const char *s) {
    for (unsigned i = 0; i < r->protocols; ++i)
        if (!strcmp(r->protocol_items[i].name, s)) return (int)i;
    return -1;
}
static int unique_selector(ios27_objc20a_registry *r, const char *s) {
    if (!valid(s)) return -1;
    for (unsigned i = 0; i < r->selectors; ++i)
        if (!strcmp(s, r->selector_items[i])) return 0;
    if (r->selectors == REG_SELECTORS) return -1;
    strcpy(r->selector_items[r->selectors++], s);
    return 0;
}
static int valid_methods(const ios27_objc20a_method *ms, uint32_t count) {
    if (count > IOS27_OBJC20A_METHODS) return 0;
    for (uint32_t i = 0; i < count; ++i)
        if (!valid(ms[i].name) || !valid(ms[i].encoding)) return 0;
    return 1;
}
static int valid_properties(const ios27_objc20a_property *p, uint32_t count) {
    if (count > IOS27_OBJC20A_PROPERTIES) return 0;
    for (uint32_t i = 0; i < count; ++i)
        if (!valid(p[i].name) || !valid(p[i].attributes)) return 0;
    return 1;
}
static int valid_adoptions(char (*names)[IOS27_OBJC20A_NAME], uint32_t count,
                           const ios27_objc20a_registry *r) {
    if (count > IOS27_OBJC20A_ADOPTIONS) return 0;
    for (uint32_t i = 0; i < count; ++i)
        if (!valid(names[i]) || protocol_index(r, names[i]) < 0) return 0;
    return 1;
}
static int check_class_graph(const ios27_objc20a_registry *r) {
    for (unsigned i = 0; i < r->classes; ++i) {
        const ios27_objc20a_class *c = &r->class_items[i];
        if (!valid(c->name) || c->instance_size > 4096 ||
            !valid_methods(c->methods, c->method_count) ||
            !valid_properties(c->properties, c->property_count) ||
            !valid_adoptions((char (*)[IOS27_OBJC20A_NAME])c->protocols,
                             c->protocol_count, r) || c->ivar_count > IOS27_OBJC20A_IVARS)
            return -1;
        for (unsigned j = 0; j < c->ivar_count; ++j)
            if (!valid(c->ivars[j].name) || !valid(c->ivars[j].encoding) ||
                c->ivars[j].size == 0 || c->ivars[j].size > 4096 ||
                c->ivars[j].alignment_log2 > 5) return -1;
        int current = (int)i;
        for (unsigned depth = 0; depth <= r->classes; ++depth) {
            const char *parent = r->class_items[current].superclass;
            if (!parent[0]) break;
            if (!valid(parent)) return -1;
            current = class_index(r, parent);
            if (current < 0 || depth == r->classes) return -1;
            if (r->class_items[current].instance_size > c->instance_size &&
                c->ivar_count == 0) {
                /* Non-fragile ivars still require a post-link offset patch;
                 * do not assume object size was already finalized. */
            }
        }
    }
    return 0;
}
static int check_protocol_graph(const ios27_objc20a_registry *r) {
    for (unsigned i = 0; i < r->protocols; ++i) {
        const ios27_objc20a_protocol *p = &r->protocol_items[i];
        if (!valid(p->name) || !valid_methods(p->methods, p->method_count) ||
            !valid_properties(p->properties, p->property_count) ||
            !valid_adoptions((char (*)[IOS27_OBJC20A_NAME])p->protocols,
                             p->protocol_count, r)) return -1;
        /* Reject adoption cycles using bounded walk over each reachable path. */
        uint8_t reach[REG_PROTOCOLS] = {0};
        uint8_t queue[REG_PROTOCOLS];
        unsigned n = 0;
        queue[n++] = (uint8_t)i;
        for (unsigned at = 0; at < n; ++at) {
            unsigned id = queue[at];
            if (reach[id]) continue;
            reach[id] = 1;
            const ios27_objc20a_protocol *q = &r->protocol_items[id];
            for (unsigned j = 0; j < q->protocol_count; ++j) {
                int next = protocol_index(r, q->protocols[j]);
                if (next < 0 || (unsigned)next == i) return -1;
                if (!reach[next]) {
                    if (n == REG_PROTOCOLS) return -1;
                    queue[n++] = (uint8_t)next;
                }
            }
        }
    }
    return 0;
}
int ios27_objc20a_registry_create(ios27_objc20a_registry **out) {
    if (!out) return -1;
    *out = calloc(1, sizeof(**out));
    return *out ? 0 : -1;
}
void ios27_objc20a_registry_destroy(ios27_objc20a_registry *r) { free(r); }
int ios27_objc20a_register(ios27_objc20a_registry *r,
                          const ios27_objc20a_image *im) {
    if (!r || !im || im->class_count > IOS27_OBJC20A_CLASSES ||
        im->protocol_count > IOS27_OBJC20A_PROTOCOLS ||
        im->category_count > IOS27_OBJC20A_CATEGORIES ||
        im->selector_count > IOS27_OBJC20A_SELECTORS ||
        im->class_count > REG_CLASSES - r->classes ||
        im->protocol_count > REG_PROTOCOLS - r->protocols ||
        im->category_count > REG_CATEGORIES - r->categories) return -1;
    ios27_objc20a_registry *next = malloc(sizeof(*next));
    if (!next) return -1;
    memcpy(next, r, sizeof(*next));
    int ok = 0;
    for (unsigned i = 0; i < im->protocol_count; ++i) {
        const ios27_objc20a_protocol *p = &im->protocols[i];
        if (!valid(p->name) || protocol_index(next, p->name) >= 0) goto done;
        next->protocol_items[next->protocols++] = *p;
    }
    for (unsigned i = 0; i < im->class_count; ++i) {
        const ios27_objc20a_class *c = &im->classes[i];
        if (!valid(c->name) || class_index(next, c->name) >= 0) goto done;
        next->class_items[next->classes++] = *c;
    }
    for (unsigned i = 0; i < im->category_count; ++i) {
        const ios27_objc20a_category *c = &im->categories[i];
        if (!valid(c->name) || !valid(c->class_name) ||
            class_index(next, c->class_name) < 0 ||
            !valid_methods(c->methods, c->method_count) ||
            !valid_properties(c->properties, c->property_count) ||
            !valid_adoptions((char (*)[IOS27_OBJC20A_NAME])c->protocols,
                             c->protocol_count, next)) goto done;
        for (unsigned j = 0; j < next->categories; ++j)
            if (!strcmp(c->name, next->category_items[j].name) &&
                !strcmp(c->class_name, next->category_items[j].class_name))
                goto done;
        next->category_items[next->categories++] = *c;
    }
    for (unsigned i = 0; i < im->selector_count; ++i)
        if (unique_selector(next, im->selectors[i])) goto done;
    if (check_class_graph(next) || check_protocol_graph(next)) goto done;
    memcpy(r, next, sizeof(*r));
    ok = 1;
done:
    free(next);
    return ok ? 0 : -1;
}
int ios27_objc20a_register_image(ios27_objc20a_registry *r,
                                const uint8_t *file, size_t size,
                                char *err, size_t cap) {
    if (!r) return -1;
    ios27_objc20a_image *im = calloc(1, sizeof(*im));
    if (!im) return -1;
    int ret = ios27_objc20a_inspect(file, size, im, err, cap);
    if (!ret) ret = ios27_objc20a_register(r, im);
    if (ret && err && cap && !err[0]) {
        const char *failure = "registration rejected (duplicate, unresolved dependency or quota)";
        size_t len = strlen(failure);
        if (len >= cap) len = cap - 1;
        memcpy(err, failure, len);
        err[len] = 0;
    }
    free(im);
    return ret;
}
int ios27_objc20a_find_class(const ios27_objc20a_registry *r,
                            const char *name, ios27_objc20a_class *out) {
    if (!r || !name || !out) return -1;
    int id = class_index(r, name);
    if (id < 0) return -1;
    *out = r->class_items[id];
    return 0;
}
void ios27_objc20a_counts(const ios27_objc20a_registry *r,
                         size_t *c, size_t *p, size_t *cat, size_t *sel) {
    if (c) *c = r ? r->classes : 0;
    if (p) *p = r ? r->protocols : 0;
    if (cat) *cat = r ? r->categories : 0;
    if (sel) *sel = r ? r->selectors : 0;
}
