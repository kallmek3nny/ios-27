#include "ios27/phase24c_registry.h"
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct phase24c_registry {
    pthread_mutex_t mutex;
    ios27_objc20a_image *images[PHASE24C_MAX_IMAGES];
    phase24c_stats stats;
};

static int name_ok(const char *p, size_t cap, int allow_empty) {
    if (!p) return 0;
    for (size_t i = 0; i < cap; ++i) {
        unsigned char ch = (unsigned char)p[i];
        if (!ch) return allow_empty || i != 0u;
        if (ch < 33u || ch > 126u) return 0;
    }
    return 0;
}
static int digits(const char **p) {
    size_t n = 0;
    while (**p >= '0' && **p <= '9') {
        if (++n > 6u) return -1;
        ++*p;
    }
    return 0;
}
static int word(const char **p) {
    switch (**p) {
        case 'Q': case 'q': case '@': case '#': case ':': ++*p; return 1;
        case '^':
            ++*p;
            if (**p != 'v') return 0;
            ++*p;
            return 1;
        default: return 0;
    }
}
/* Strict whitelist of complete Clang ObjC2 type encodings. Rejects mixed
 * FP, small scalars, unions, structs, blocks, qualifiers, variadics, vectors. */
phase24c_abi phase24c_encoding(const char *encoding) {
    if (!name_ok(encoding, IOS27_OBJC20A_NAME, 0)) return PHASE24C_UNSUPPORTED;
    const char *p = encoding;
    int ret = 0;
    if (*p == 'v') { ret = 1; ++p; }
    else if (*p == 'f') { ret = 2; ++p; }
    else if (*p == 'd') { ret = 3; ++p; }
    else if (word(&p)) ret = 4;
    else return PHASE24C_UNSUPPORTED;
    if (digits(&p) || *p++ != '@' || digits(&p) ||
        *p++ != ':' || digits(&p)) return PHASE24C_UNSUPPORTED;
    size_t args = 0;
    if (ret == 2 || ret == 3) {
        if (*p++ != (ret == 2 ? 'f' : 'd') || digits(&p) || *p)
            return PHASE24C_UNSUPPORTED;
        return ret == 2 ? PHASE24C_FLOAT1 : PHASE24C_DOUBLE1;
    }
    while (*p) {
        if (args == 4u || !word(&p) || digits(&p))
            return PHASE24C_UNSUPPORTED;
        ++args;
    }
    if (ret == 1) return args == 0u ? PHASE24C_VOID0 : PHASE24C_UNSUPPORTED;
    return (phase24c_abi)(PHASE24C_WORD0 + args);
}
static const ios27_objc20a_class *find_class(
    const phase24c_registry *r, const ios27_objc20a_image *candidate,
    const char *name) {
    for (size_t i = 0; i <= r->stats.images; ++i) {
        const ios27_objc20a_image *im = i == r->stats.images ?
            candidate : r->images[i];
        if (!im) continue;
        for (size_t j = 0; j < im->class_count; ++j)
            if (!strcmp(im->classes[j].name, name)) return &im->classes[j];
    }
    return NULL;
}
static int methods_ok(const ios27_objc20a_method *m, size_t count,
                      int descriptions) {
    for (size_t i = 0; i < count; ++i) {
        if (!name_ok(m[i].name, IOS27_OBJC20A_NAME, 0) ||
            !name_ok(m[i].encoding, IOS27_OBJC20A_NAME, 0) ||
            phase24c_encoding(m[i].encoding) == PHASE24C_UNSUPPORTED ||
            m[i].class_method > 1u || m[i].optional > 1u ||
            (!descriptions && (m[i].implementation & 3u)) ||
            (descriptions && m[i].implementation)) return -1;
        for (size_t j = 0; j < i; ++j)
            if (m[i].class_method == m[j].class_method &&
                m[i].optional == m[j].optional &&
                !strcmp(m[i].name, m[j].name)) return -1;
    }
    return 0;
}
static int protocols_ok(const char (*names)[IOS27_OBJC20A_NAME], size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (!name_ok(names[i], IOS27_OBJC20A_NAME, 0)) return -1;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(names[i], names[j])) return -1;
    }
    return 0;
}
static int validate(const phase24c_registry *r, const ios27_objc20a_image *im) {
    if (!im || im->class_count > IOS27_OBJC20A_CLASSES ||
        im->category_count > IOS27_OBJC20A_CATEGORIES ||
        im->protocol_count > IOS27_OBJC20A_PROTOCOLS ||
        im->selector_count > IOS27_OBJC20A_SELECTORS ||
        r->stats.images == PHASE24C_MAX_IMAGES ||
        im->class_count > PHASE24C_MAX_CLASSES - r->stats.classes) return -1;
    for (size_t i = 0; i < im->class_count; ++i) {
        const ios27_objc20a_class *c = &im->classes[i];
        if (!name_ok(c->name, IOS27_OBJC20A_NAME, 0) ||
            !name_ok(c->superclass, IOS27_OBJC20A_NAME, 1) ||
            !c->instance_size || c->instance_size > 4096u ||
            c->method_count > IOS27_OBJC20A_METHODS ||
            c->ivar_count > IOS27_OBJC20A_IVARS ||
            c->property_count > IOS27_OBJC20A_PROPERTIES ||
            c->protocol_count > IOS27_OBJC20A_ADOPTIONS ||
            methods_ok(c->methods, c->method_count, 0) ||
            protocols_ok(c->protocols, c->protocol_count) ||
            find_class(r, NULL, c->name)) return -1;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(im->classes[j].name, c->name)) return -1;
        for (size_t j = 0; j < c->ivar_count; ++j) {
            const ios27_objc20a_ivar *v = &c->ivars[j];
            if (!name_ok(v->name, IOS27_OBJC20A_NAME, 0) ||
                !name_ok(v->encoding, IOS27_OBJC20A_NAME, 0) ||
                !v->size || v->alignment_log2 > 5u ||
                v->offset > c->instance_size ||
                v->size > c->instance_size - v->offset ||
                (v->offset & ((1u << v->alignment_log2) - 1u))) return -1;
            for (size_t k = 0; k < j; ++k) {
                const ios27_objc20a_ivar *old = &c->ivars[k];
                if (!strcmp(v->name, old->name) ||
                    (v->offset < old->offset + old->size &&
                     old->offset < v->offset + v->size)) return -1;
            }
        }
    }
    for (size_t i = 0; i < im->category_count; ++i) {
        const ios27_objc20a_category *c = &im->categories[i];
        if (!name_ok(c->name, IOS27_OBJC20A_NAME, 0) ||
            !name_ok(c->class_name, IOS27_OBJC20A_NAME, 0) ||
            c->method_count > IOS27_OBJC20A_METHODS ||
            c->protocol_count > IOS27_OBJC20A_ADOPTIONS ||
            c->property_count > IOS27_OBJC20A_PROPERTIES ||
            methods_ok(c->methods, c->method_count, 0) ||
            protocols_ok(c->protocols, c->protocol_count)) return -1;
    }
    for (size_t i = 0; i < im->protocol_count; ++i) {
        const ios27_objc20a_protocol *p = &im->protocols[i];
        if (!name_ok(p->name, IOS27_OBJC20A_NAME, 0) ||
            p->method_count > IOS27_OBJC20A_METHODS ||
            p->protocol_count > IOS27_OBJC20A_ADOPTIONS ||
            p->property_count > IOS27_OBJC20A_PROPERTIES ||
            methods_ok(p->methods, p->method_count, 1) ||
            protocols_ok(p->protocols, p->protocol_count)) return -1;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(p->name, im->protocols[j].name)) return -1;
    }
    for (size_t i = 0; i < im->selector_count; ++i)
        if (!name_ok(im->selectors[i], IOS27_OBJC20A_NAME, 0)) return -1;
    /* Prevent cycles even if an external superclass was resolved only now. */
    for (size_t i = 0; i < im->class_count; ++i) {
        const ios27_objc20a_class *c = &im->classes[i];
        for (size_t step = 0; c && c->superclass[0]; ++step) {
            if (step >= PHASE24C_MAX_CLASSES) return -1;
            c = find_class(r, im, c->superclass);
            if (c && !strcmp(c->name, im->classes[i].name)) return -1;
        }
    }
    return 0;
}
int phase24c_create(phase24c_registry **out) {
    if (!out) return -1;
    *out = NULL;
    phase24c_registry *r = calloc(1u, sizeof(*r));
    if (!r) return -1;
    if (pthread_mutex_init(&r->mutex, NULL)) { free(r); return -1; }
    *out = r;
    return 0;
}
void phase24c_destroy(phase24c_registry *r) {
    if (!r) return;
    for (size_t i = 0; i < r->stats.images; ++i) free(r->images[i]);
    pthread_mutex_destroy(&r->mutex);
    free(r);
}
int phase24c_register(phase24c_registry *r, const ios27_objc20a_image *im) {
    if (!r || !im || pthread_mutex_lock(&r->mutex)) return -1;
    int rc = -1;
    if (!validate(r, im)) {
        ios27_objc20a_image *copy = malloc(sizeof(*copy));
        if (copy) {
            memcpy(copy, im, sizeof(*copy));
            r->images[r->stats.images++] = copy;
            r->stats.classes += copy->class_count;
            r->stats.categories += copy->category_count;
            r->stats.protocols += copy->protocol_count;
            ++r->stats.registrations;
            rc = 0;
        }
    }
    if (rc) ++r->stats.rejections;
    pthread_mutex_unlock(&r->mutex);
    return rc;
}
static int find_method(const ios27_objc20a_method *methods, size_t count,
                       const char *selector, int is_class,
                       ios27_objc20a_method *out) {
    for (size_t j = 0; j < count; ++j)
        if (methods[j].class_method == is_class &&
            !strcmp(methods[j].name, selector)) {
            *out = methods[j];
            return 0;
        }
    return -1;
}
int phase24c_resolve(phase24c_registry *r, const char *dynamic_class,
                     const char *selector, int class_method,
                     const char *lexical_class, phase24c_resolution *out) {
    if (out) memset(out, 0, sizeof(*out));
    if (!r || !out || !name_ok(dynamic_class, IOS27_OBJC20A_NAME, 0) ||
        !name_ok(selector, IOS27_OBJC20A_NAME, 0) ||
        (class_method != 0 && class_method != 1) ||
        (lexical_class && !name_ok(lexical_class, IOS27_OBJC20A_NAME, 0)) ||
        pthread_mutex_lock(&r->mutex)) return -1;
    ++r->stats.lookups;
    const ios27_objc20a_class *c = find_class(r, NULL, dynamic_class);
    int rc = -1;
    if (lexical_class) {
        /* Prove the lexical class lies on the receiver's dynamic ancestry. */
        size_t depth = 0;
        while (c && strcmp(c->name, lexical_class)) {
            if (!c->superclass[0] || ++depth > PHASE24C_MAX_CLASSES) goto done;
            c = find_class(r, NULL, c->superclass);
        }
        if (!c || !c->superclass[0]) goto done;
        c = find_class(r, NULL, c->superclass);
        if (!c) { rc = -2; goto done; }
    }
    for (size_t depth = 0; c && depth < PHASE24C_MAX_CLASSES; ++depth) {
        /* Most recently loaded category has precedence, then own methods. */
        for (size_t i = r->stats.images; i > 0; --i) {
            const ios27_objc20a_image *im = r->images[i - 1u];
            for (size_t j = im->category_count; j > 0; --j) {
                const ios27_objc20a_category *cat = &im->categories[j - 1u];
                if (!strcmp(cat->class_name, c->name) &&
                    !find_method(cat->methods, cat->method_count,
                                 selector, class_method, &out->method)) {
                    out->image_index = (uint32_t)(i - 1u);
                    out->category = 1u;
                    goto found;
                }
            }
        }
        for (size_t i = 0; i < r->stats.images; ++i) {
            const ios27_objc20a_image *im = r->images[i];
            for (size_t j = 0; j < im->class_count; ++j)
                if (!strcmp(im->classes[j].name, c->name) &&
                    !find_method(im->classes[j].methods,
                                 im->classes[j].method_count,
                                 selector, class_method, &out->method)) {
                    out->image_index = (uint32_t)i;
                    goto found;
                }
        }
        if (!c->superclass[0]) goto done;
        const ios27_objc20a_class *parent = find_class(r, NULL, c->superclass);
        if (!parent) { rc = -2; goto done; }
        c = parent;
    }
    goto done;
found:
    out->abi = phase24c_encoding(out->method.encoding);
    rc = out->abi == PHASE24C_UNSUPPORTED ? -1 : 0;
done:
    if (rc == -2) ++r->stats.unresolved_superclasses;
    pthread_mutex_unlock(&r->mutex);
    return rc;
}
int phase24c_statistics(phase24c_registry *r, phase24c_stats *out) {
    if (!r || !out || pthread_mutex_lock(&r->mutex)) return -1;
    *out = r->stats;
    pthread_mutex_unlock(&r->mutex);
    return 0;
}
