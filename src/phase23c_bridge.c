#include "ios27/phase23c_bridge.h"
#include "ios27/phase22d_metadata.h"
#include "ios27/objc_metadata.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char name[PHASE23C_IMAGE_NAME];
    uint64_t generation;
    size_t count, live, active;
    char classes[PHASE23C_IMAGE_CLASSES][PHASE22D_NAME];
} image_entry;
typedef struct {
    void *pointer;
    uint32_t references;
    uint16_t owner;
    uint8_t state; /* 1 live, 2 destroyed */
} object_entry;
struct phase23c_bridge {
    pthread_mutex_t lock;
    phase22d_runtime *substrate;
    image_entry images[PHASE23C_IMAGES];
    object_entry *objects;
    size_t image_count, object_count, live, active;
    phase23c_stats counters;
};
static int reject(char *dest, size_t n, const char *reason) {
    if (dest && n) (void)snprintf(dest, n, "%s", reason);
    return -1;
}
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static uint64_t u64(const uint8_t *p) {
    return (uint64_t)u32(p) | ((uint64_t)u32(p + 4u) << 32u);
}
/* Bounded scan: the underlying 22d metadata inspector cannot validate
 * protocol/category/+load or external class-relocation semantics. */
static int forbidden(const uint8_t *p, size_t n) {
    static const char *const names[] = {
        "__objc_protolist", "__objc_catlist", "__objc_nlclslist",
        "__objc_nlcatlist", "__objc_protorefs", "__objc_classrefs",
        "__objc_superrefs"
    };
    if (n < 32u || n > 2u * 1024u * 1024u ||
        u32(p) != 0xfeedfacfu || u32(p + 12u) != 1u) return -1;
    uint32_t count = u32(p + 16u), bytes = u32(p + 20u);
    if (count > 256u || bytes > n - 32u) return -1;
    size_t pos = 32u, end = 32u + bytes;
    for (uint32_t c = 0; c < count; ++c) {
        if (pos > end || end - pos < 8u) return -1;
        uint32_t kind = u32(p + pos), size = u32(p + pos + 4u);
        if (size < 8u || size > end - pos) return -1;
        if (kind == 0x19u) {
            if (size < 72u) return -1;
            uint32_t sections = u32(p + pos + 64u);
            if (sections > (size - 72u) / 80u) return -1;
            for (uint32_t i = 0; i < sections; ++i) {
                const uint8_t *sec = p + pos + 72u + i * 80u;
                for (size_t j = 0; j < sizeof(names)/sizeof(*names); ++j)
                    if (!strncmp((const char *)sec, names[j], 16u) &&
                        u64(sec + 40u)) return 1;
            }
        }
        pos += size;
    }
    return pos == end ? 0 : -1;
}
static int valid_name(const char *p, size_t limit) {
    if (!p) return 0;
    for (size_t i = 0; i < limit; ++i) {
        unsigned char c = (unsigned char)p[i];
        if (!c) return i > 0u;
        if (c < 33u || c > 126u) return 0;
    }
    return 0;
}
static image_entry *image_locked(phase23c_bridge *b, phase23c_image h) {
    if (!h.generation || h.slot >= b->image_count) return NULL;
    image_entry *e = &b->images[h.slot];
    return e->generation == h.generation ? e : NULL;
}
static int owns_class(const image_entry *e, const char *name) {
    for (size_t i = 0; i < e->count; ++i)
        if (!strcmp(name, e->classes[i])) return 1;
    return 0;
}
static object_entry *object_locked(phase23c_bridge *b, const void *p) {
    if (!p) return NULL;
    for (size_t i = b->object_count; i; --i)
        if (b->objects[i-1u].pointer == p) return &b->objects[i-1u];
    return NULL;
}
int phase23c_create(phase23c_bridge **out) {
    if (!out) return -1;
    *out = NULL;
    phase23c_bridge *b = calloc(1u, sizeof(*b));
    if (!b) return -1;
    b->objects = calloc(PHASE23C_HANDLES, sizeof(*b->objects));
    if (!b->objects) { free(b); return -1; }
    if (pthread_mutex_init(&b->lock, NULL)) {
        free(b->objects); free(b); return -1;
    }
    if (phase22d_create(&b->substrate)) {
        pthread_mutex_destroy(&b->lock);
        free(b->objects); free(b); return -1;
    }
    *out = b;
    return 0;
}
int phase23c_destroy(phase23c_bridge *b) {
    if (!b || pthread_mutex_lock(&b->lock)) return -1;
    phase22d_stats s;
    int ok = !b->live && !b->active &&
             !phase22d_statistics(b->substrate, &s) &&
             !s.live_objects && !s.weak_locations &&
             !phase22d_validate(b->substrate);
    pthread_mutex_unlock(&b->lock);
    if (!ok) return -1;
    /* Callers must quiesce other threads before destroy. */
    phase22d_destroy(b->substrate);
    pthread_mutex_destroy(&b->lock);
    free(b->objects); free(b);
    return 0;
}
int phase23c_import(phase23c_bridge *b, const char *name,
                    const uint8_t *bytes, size_t length,
                    const phase22d_class_spec *trusted, size_t count,
                    phase23c_image *out, char *diag, size_t cap) {
    int rc = -1;
    if (out) *out = (phase23c_image){0, 0};
    if (!b || !out || !bytes || !length || !trusted || !count ||
        count > PHASE23C_IMAGE_CLASSES ||
        !valid_name(name, PHASE23C_IMAGE_NAME))
        return reject(diag, cap, "invalid image registration");
    int section_rc = forbidden(bytes, length);
    if (section_rc)
        return reject(diag, cap, section_rc > 0 ?
            "unsupported ObjC2 protocol/category/+load metadata" :
            "invalid Mach-O load-command table");
    ios27_objc_metadata *meta = calloc(1u, sizeof(*meta));
    if (!meta) return reject(diag, cap, "metadata allocation failed");
    if (ios27_objc_metadata_inspect(bytes, length, meta, diag, cap))
        goto finish;
    if (meta->class_count != count) {
        (void)reject(diag, cap, "class manifest count mismatch");
        goto finish;
    }
    if (pthread_mutex_lock(&b->lock)) goto finish;
    if (b->image_count == PHASE23C_IMAGES) {
        (void)reject(diag, cap, "image quota exceeded"); goto unlock;
    }
    for (size_t i = 0; i < b->image_count; ++i) {
        if (!strcmp(b->images[i].name, name)) {
            (void)reject(diag, cap, "duplicate image"); goto unlock;
        }
        for (size_t k = 0; k < count; ++k)
            if (trusted[k].name &&
                owns_class(&b->images[i], trusted[k].name)) {
                (void)reject(diag, cap, "duplicate class across images");
                goto unlock;
            }
    }
    for (size_t k = 0; k < count; ++k) {
        if (!valid_name(trusted[k].name, PHASE22D_NAME)) {
            (void)reject(diag, cap, "invalid class name"); goto unlock;
        }
        for (size_t j = 0; j < k; ++j)
            if (!strcmp(trusted[k].name, trusted[j].name)) {
                (void)reject(diag, cap, "duplicate class manifest"); goto unlock;
            }
    }
    if (phase22d_register_compiled(b->substrate, bytes, length,
                                   trusted, count, diag, cap)) goto unlock;
    image_entry *e = &b->images[b->image_count];
    memcpy(e->name, name, strlen(name) + 1u);
    e->generation = 1u;
    e->count = count;
    for (size_t k = 0; k < count; ++k)
        memcpy(e->classes[k], trusted[k].name, strlen(trusted[k].name) + 1u);
    *out = (phase23c_image){(uint16_t)b->image_count, e->generation};
    ++b->image_count;
    ++b->counters.image_imports;
    rc = 0;
unlock:
    if (rc) ++b->counters.failed_imports;
    pthread_mutex_unlock(&b->lock);
finish:
    free(meta);
    return rc;
}
int phase23c_category(phase23c_bridge *b, phase23c_image h,
                      const phase22d_category *cat) {
    if (!b || !cat || !cat->class_name || pthread_mutex_lock(&b->lock))
        return -1;
    int exists = 0;
    for (size_t i = 0; i < b->image_count; ++i)
        if (owns_class(&b->images[i], cat->class_name)) exists = 1;
    int rc = image_locked(b, h) && exists ?
             phase22d_install_category(b->substrate, cat) : -1;
    if (!rc) ++b->counters.category_replacements;
    pthread_mutex_unlock(&b->lock);
    return rc;
}
int phase23c_allocate(phase23c_bridge *b, phase23c_image h,
                      const char *name, void **out) {
    if (out) *out = NULL;
    if (!b || !out || !name || pthread_mutex_lock(&b->lock)) return -1;
    image_entry *e = image_locked(b, h);
    int rc = -1;
    if (e && owns_class(e, name) && b->object_count < PHASE23C_HANDLES &&
        !phase22d_allocate(b->substrate, name, out)) {
        b->objects[b->object_count++] = (object_entry){*out, 1u, h.slot, 1u};
        ++e->live; ++b->live; ++b->counters.allocations;
        rc = 0;
    }
    pthread_mutex_unlock(&b->lock);
    return rc;
}
int phase23c_retain(phase23c_bridge *b, const void *p) {
    if (!b || pthread_mutex_lock(&b->lock)) return -1;
    object_entry *o = object_locked(b, p);
    int rc = -1;
    if (o && o->state == 1u && o->references < UINT32_MAX &&
        !phase22d_retain(b->substrate, p)) {
        ++o->references; ++b->counters.retains; rc = 0;
    }
    pthread_mutex_unlock(&b->lock);
    return rc;
}
int phase23c_release(phase23c_bridge *b, const void *p) {
    if (!b || pthread_mutex_lock(&b->lock)) return -1;
    object_entry *o = object_locked(b, p);
    if (!o || o->state != 1u || !o->references) {
        pthread_mutex_unlock(&b->lock); return -1;
    }
    if (--o->references == 0u) {
        o->state = 2u;
        --b->images[o->owner].live; --b->live;
    }
    ++b->counters.releases;
    pthread_mutex_unlock(&b->lock);
    /* Deallocator callbacks must run outside the bridge mutex. */
    return phase22d_release(b->substrate, p);
}
int phase23c_selector(phase23c_bridge *b, const char *name,
                      phase22d_sel *out) {
    return b ? phase22d_selector(b->substrate, name, out) : -1;
}
static int send_begin(phase23c_bridge *b, void *p, uint16_t *owner) {
    if (pthread_mutex_lock(&b->lock)) return -1;
    object_entry *o = object_locked(b, p);
    if (!o || o->state != 1u || o->references == UINT32_MAX ||
        phase22d_retain(b->substrate, p)) {
        ++b->counters.failed_sends;
        pthread_mutex_unlock(&b->lock); return -1;
    }
    ++o->references; ++b->active; ++b->images[o->owner].active;
    *owner = o->owner;
    pthread_mutex_unlock(&b->lock);
    return 0;
}
static void send_end(phase23c_bridge *b, void *p, uint16_t owner, int rc) {
    if (!pthread_mutex_lock(&b->lock)) {
        object_entry *o = object_locked(b, p);
        if (o && o->state == 1u && o->references) {
            if (--o->references == 0) {
                o->state = 2u;
                --b->live;
                --b->images[owner].live;
            }
        }
        --b->active; --b->images[owner].active;
        if (rc) ++b->counters.failed_sends;
        else ++b->counters.sends;
        pthread_mutex_unlock(&b->lock);
    }
    (void)phase22d_release(b->substrate, p);
}
int phase23c_send_words(phase23c_bridge *b, void *p, phase22d_sel selector,
                        const char *super, const uintptr_t *args,
                        size_t count, uintptr_t *out) {
    uint16_t owner;
    if (!b || !out || count > 4u || (count && !args) ||
        send_begin(b, p, &owner)) return -1;
    int rc = phase22d_send_words(b->substrate, p, selector,
                                super, args, count, out);
    send_end(b, p, owner, rc);
    return rc;
}
int phase23c_send_double1(phase23c_bridge *b, void *p,
                          phase22d_sel selector, const char *super,
                          double arg, double *out) {
    uint16_t owner;
    if (!b || !out || send_begin(b, p, &owner)) return -1;
    int rc = phase22d_send_double1(b->substrate, p, selector, super, arg, out);
    send_end(b, p, owner, rc);
    return rc;
}
int phase23c_weak_store(phase23c_bridge *b, void **slot, void *p) {
    if (!b || !slot || pthread_mutex_lock(&b->lock)) return -1;
    object_entry *o = object_locked(b, p);
    int ok = !p || (o && o->state == 1u);
    if (ok) ok = !phase22d_weak_store(b->substrate, slot, p);
    pthread_mutex_unlock(&b->lock);
    return ok ? 0 : -1;
}
int phase23c_weak_load_retained(phase23c_bridge *b, void **slot, void **out) {
    if (out) *out = NULL;
    if (!b || !slot || !out || pthread_mutex_lock(&b->lock)) return -1;
    int rc = phase22d_weak_load_retained(b->substrate, slot, out);
    if (!rc && *out) {
        object_entry *o = object_locked(b, *out);
        if (!o || o->state != 1u || o->references == UINT32_MAX) {
            (void)phase22d_release(b->substrate, *out);
            *out = NULL; rc = -1;
        } else ++o->references;
    }
    pthread_mutex_unlock(&b->lock);
    return rc;
}
int phase23c_weak_unregister(phase23c_bridge *b, void **slot) {
    return b ? phase22d_weak_unregister(b->substrate, slot) : -1;
}
int phase23c_statistics(phase23c_bridge *b, phase23c_stats *out) {
    if (!b || !out || pthread_mutex_lock(&b->lock)) return -1;
    *out = b->counters;
    out->images = b->image_count; out->tracked_objects = b->object_count;
    out->live_objects = b->live; out->active_sends = b->active;
    int rc = phase22d_statistics(b->substrate, &out->substrate);
    pthread_mutex_unlock(&b->lock);
    return rc;
}
int phase23c_validate(phase23c_bridge *b) {
    if (!b || pthread_mutex_lock(&b->lock)) return -1;
    size_t live = 0, per_image[PHASE23C_IMAGES] = {0};
    int rc = 0;
    for (size_t i = 0; i < b->object_count; ++i) {
        const object_entry *o = &b->objects[i];
        if (o->owner >= b->image_count || !o->pointer) { rc = -1; break; }
        if (o->state == 1u && o->references) {
            ++live; ++per_image[o->owner];
        } else if (o->state != 2u || o->references) rc = -1;
    }
    if (live != b->live || b->image_count > PHASE23C_IMAGES ||
        b->object_count > PHASE23C_HANDLES || b->active > live) rc = -1;
    for (size_t i = 0; i < b->image_count; ++i)
        if (per_image[i] != b->images[i].live ||
            b->images[i].active > b->images[i].live) rc = -1;
    if (phase22d_validate(b->substrate)) rc = -1;
    pthread_mutex_unlock(&b->lock);
    return rc;
}
int phase23c_unload(phase23c_bridge *b, phase23c_image h) {
    if (!b || pthread_mutex_lock(&b->lock)) return -1;
    /* No class unregister API exists in read-only 22d. */
    int rc = image_locked(b, h) ? -2 : -1;
    pthread_mutex_unlock(&b->lock);
    return rc;
}
