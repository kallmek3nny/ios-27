#include "ios27/phase22d_runtime.h"
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint16_t selector;
    phase22d_signature signature;
    phase22d_implementation implementation;
} method_entry;
typedef struct {
    char name[PHASE22D_NAME];
    int16_t parent;
    size_t bytes;
    uintptr_t class_tag, metaclass_tag;
    method_entry instances[PHASE22D_METHODS], classes[PHASE22D_METHODS];
    uint16_t instance_count, class_count;
    phase22d_initializer initializer;
    phase22d_deallocator deallocator;
    void *context;
} class_entry;
typedef struct {
    void *address;
    size_t bytes;
    uint32_t references;
    uint16_t type;
    uint8_t state; /* 1 live; 2 deallocating; 3 tombstone */
} object_entry;
typedef struct { void **slot; int32_t object_index; } weak_entry;

struct phase22d_runtime {
    pthread_mutex_t mutex;
    pthread_cond_t initialized;
    int init_depth;
    pthread_t init_owner;
    uint8_t init_state[PHASE22D_CLASSES]; /* unstarted, in progress, complete */
    char selectors[PHASE22D_SELECTORS][PHASE22D_NAME];
    class_entry classes[PHASE22D_CLASSES];
    object_entry *objects;
    weak_entry *weak;
    size_t nclasses, nselectors, nobjects, nweak, live, memory;
    phase22d_stats metrics;
};

static int valid_name(const char *s) {
    if (!s) return 0;
    for (size_t i = 0; i < PHASE22D_NAME; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!c) return i != 0u;
        if (c < 33u || c > 126u) return 0;
    }
    return 0;
}
static int class_index(const class_entry *classes, size_t n, const char *name) {
    if (!valid_name(name)) return -1;
    for (size_t i = 0; i < n; ++i)
        if (!strcmp(classes[i].name, name)) return (int)i;
    return -1;
}
static int selector_index(const phase22d_runtime *r, phase22d_sel selector) {
    if (!selector) return -1;
    for (size_t i = 0; i < r->nselectors; ++i)
        if (selector == (const void *)r->selectors[i]) return (int)i;
    return -1;
}
static int intern(char (*sels)[PHASE22D_NAME], size_t *count,
                  const char *name) {
    if (!valid_name(name)) return -1;
    for (size_t i = 0; i < *count; ++i)
        if (!strcmp(sels[i], name)) return (int)i;
    if (*count == PHASE22D_SELECTORS) return -1;
    size_t index = (*count)++;
    memcpy(sels[index], name, strlen(name) + 1u);
    return (int)index;
}
static int valid_imp(const phase22d_method_spec *m) {
    if (!m || !valid_name(m->selector)) return 0;
    switch (m->signature) {
    case PHASE22D_WORD0: return m->implementation.word0 != NULL;
    case PHASE22D_WORD1: return m->implementation.word1 != NULL;
    case PHASE22D_WORD2: return m->implementation.word2 != NULL;
    case PHASE22D_WORD3: return m->implementation.word3 != NULL;
    case PHASE22D_WORD4: return m->implementation.word4 != NULL;
    case PHASE22D_DOUBLE1: return m->implementation.double1 != NULL;
    default: return 0;
    }
}
static int add_methods(char (*sels)[PHASE22D_NAME], size_t *nsel,
                       method_entry *target, uint16_t *nmethod,
                       const phase22d_method_spec *methods, size_t count,
                       int replace) {
    if (count > PHASE22D_METHODS || (count && !methods)) return -1;
    for (size_t i = 0; i < count; ++i) {
        int id;
        if (!valid_imp(&methods[i]) ||
            (id = intern(sels, nsel, methods[i].selector)) < 0) return -1;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(methods[j].selector, methods[i].selector)) return -1;
        size_t slot = 0;
        while (slot < *nmethod && target[slot].selector != (uint16_t)id)
            ++slot;
        if (slot < *nmethod && !replace) return -1;
        if (slot == *nmethod) {
            if (*nmethod == PHASE22D_METHODS) return -1;
            ++(*nmethod);
        }
        target[slot] = (method_entry){(uint16_t)id, methods[i].signature,
                                      methods[i].implementation};
    }
    return 0;
}
int phase22d_create(phase22d_runtime **out) {
    phase22d_runtime *r;
    if (!out) return -1;
    *out = NULL;
    r = calloc(1u, sizeof(*r));
    if (!r) return -1;
    r->objects = calloc(PHASE22D_OBJECTS, sizeof(*r->objects));
    r->weak = calloc(PHASE22D_WEAK_LIMIT, sizeof(*r->weak));
    if (!r->objects || !r->weak) {
        free(r->objects); free(r->weak); free(r);
        return -1;
    }
    if (pthread_mutex_init(&r->mutex, NULL)) {
        free(r->objects); free(r->weak); free(r); return -1;
    }
    if (pthread_cond_init(&r->initialized, NULL)) {
        pthread_mutex_destroy(&r->mutex);
        free(r->objects); free(r->weak); free(r); return -1;
    }
    *out = r;
    return 0;
}
void phase22d_destroy(phase22d_runtime *r) {
    if (!r) return;
    /* Caller must join workers before destroying runtime, as with libobjc. */
    for (size_t i = 0; i < r->nobjects; ++i) free(r->objects[i].address);
    pthread_cond_destroy(&r->initialized);
    pthread_mutex_destroy(&r->mutex);
    free(r->objects); free(r->weak); free(r);
}
int phase22d_register(phase22d_runtime *r,
                      const phase22d_class_spec *specs, size_t n) {
    int ok = -1;
    class_entry *stage = NULL;
    char (*sels)[PHASE22D_NAME] = NULL;
    if (!r || !specs || !n) return -1;
    if (pthread_mutex_lock(&r->mutex)) return -1;
    if (r->init_depth || n > PHASE22D_CLASSES - r->nclasses) goto done;
    stage = malloc(sizeof(r->classes));
    sels = malloc(sizeof(r->selectors));
    if (!stage || !sels) goto done;
    memcpy(stage, r->classes, sizeof(r->classes));
    memcpy(sels, r->selectors, sizeof(r->selectors));
    size_t nsel = r->nselectors, first = r->nclasses;
    for (size_t i = 0; i < n; ++i) {
        const phase22d_class_spec *s = &specs[i];
        class_entry *c = &stage[first + i];
        if (!valid_name(s->name) ||
            class_index(stage, first + i, s->name) >= 0 ||
            (s->superclass && !valid_name(s->superclass)) ||
            s->instance_size < sizeof(void *) ||
            s->instance_size > PHASE22D_MAX_OBJECT_SIZE) goto done;
        memcpy(c->name, s->name, strlen(s->name) + 1u);
        c->parent = -1;
        c->bytes = s->instance_size;
        c->initializer = s->initialize;
        c->deallocator = s->deallocate;
        c->context = s->context;
        c->instance_count = 0;
        c->class_count = 0;
        if (add_methods(sels, &nsel, c->instances, &c->instance_count,
                        s->instance_methods, s->instance_count, 0) ||
            add_methods(sels, &nsel, c->classes, &c->class_count,
                        s->class_methods, s->class_count, 0)) goto done;
    }
    for (size_t i = 0; i < n; ++i) {
        class_entry *c = &stage[first + i];
        const char *parent = specs[i].superclass;
        if (!parent) continue;
        int p = class_index(stage, first + n, parent);
        if (p < 0 || stage[p].bytes > c->bytes) goto done;
        c->parent = (int16_t)p;
    }
    for (size_t i = first; i < first + n; ++i) {
        int p = (int)i;
        for (size_t depth = 0; p >= 0; ++depth) {
            if (depth > first + n) goto done;
            p = stage[p].parent;
        }
    }
    memcpy(r->classes, stage, sizeof(r->classes));
    memcpy(r->selectors, sels, sizeof(r->selectors));
    r->nselectors = nsel;
    r->nclasses = first + n;
    ok = 0;
done:
    free(stage);
    free(sels);
    pthread_mutex_unlock(&r->mutex);
    return ok;
}
int phase22d_install_category(phase22d_runtime *r,
                              const phase22d_category *category) {
    int ok = -1, id;
    char (*sels)[PHASE22D_NAME] = NULL;
    class_entry *stage = NULL;
    if (!r || !category || !category->count ||
        (category->class_method != 0 && category->class_method != 1)) return -1;
    if (pthread_mutex_lock(&r->mutex)) return -1;
    if (r->init_depth ||
        (id = class_index(r->classes, r->nclasses, category->class_name)) < 0)
        goto done;
    sels = malloc(sizeof(r->selectors));
    stage = malloc(sizeof(*stage));
    if (!sels || !stage) goto done;
    memcpy(sels, r->selectors, sizeof(r->selectors));
    *stage = r->classes[id];
    size_t nsel = r->nselectors;
    method_entry *entries = category->class_method ? stage->classes : stage->instances;
    uint16_t *count = category->class_method ?
                       &stage->class_count : &stage->instance_count;
    if (add_methods(sels, &nsel, entries, count,
                    category->methods, category->count, 1)) goto done;
    r->classes[id] = *stage;
    memcpy(r->selectors, sels, sizeof(r->selectors));
    r->nselectors = nsel;
    ++r->metrics.categories;
    ok = 0;
done:
    free(stage); free(sels);
    pthread_mutex_unlock(&r->mutex);
    return ok;
}
int phase22d_selector(phase22d_runtime *r, const char *name,
                      phase22d_sel *out) {
    int id = -1;
    if (!out) return -1;
    *out = NULL;
    if (!r || pthread_mutex_lock(&r->mutex)) return -1;
    id = intern(r->selectors, &r->nselectors, name);
    if (id >= 0) *out = r->selectors[id];
    pthread_mutex_unlock(&r->mutex);
    return id < 0 ? -1 : 0;
}
static int class_token(phase22d_runtime *r, const char *name,
                       void **out, int meta) {
    int id;
    if (!out) return -1;
    *out = NULL;
    if (!r || pthread_mutex_lock(&r->mutex)) return -1;
    id = class_index(r->classes, r->nclasses, name);
    if (id >= 0) *out = meta ? (void *)&r->classes[id].metaclass_tag :
                             (void *)&r->classes[id].class_tag;
    pthread_mutex_unlock(&r->mutex);
    return id < 0 ? -1 : 0;
}
int phase22d_class_object(phase22d_runtime *r, const char *name, void **out) {
    return class_token(r, name, out, 0);
}
int phase22d_metaclass_object(phase22d_runtime *r, const char *name, void **out) {
    return class_token(r, name, out, 1);
}
static int object_index(const phase22d_runtime *r, const void *ptr) {
    if (!ptr) return -1;
    for (size_t i = r->nobjects; i > 0; --i)
        if (r->objects[i - 1].address == ptr) return (int)(i - 1);
    return -1;
}
int phase22d_allocate(phase22d_runtime *r, const char *name, void **out) {
    int ok = -1, id;
    if (!out) return -1;
    *out = NULL;
    if (!r || pthread_mutex_lock(&r->mutex)) return -1;
    id = class_index(r->classes, r->nclasses, name);
    if (id >= 0 && r->nobjects < PHASE22D_OBJECTS &&
        r->live < PHASE22D_LIVE_LIMIT &&
        r->classes[id].bytes <= PHASE22D_MEMORY_LIMIT - r->memory) {
        void *address = calloc(1u, r->classes[id].bytes);
        if (address) {
            void *cls = &r->classes[id].class_tag;
            memcpy(address, &cls, sizeof(cls)); /* native isa-sized prefix */
            object_entry *o = &r->objects[r->nobjects++];
            *o = (object_entry){address, r->classes[id].bytes, 1u, (uint16_t)id, 1u};
            r->live++;
            r->memory += o->bytes;
            r->metrics.allocations++;
            *out = address;
            ok = 0;
        }
    }
    pthread_mutex_unlock(&r->mutex);
    return ok;
}
static int retain_locked(phase22d_runtime *r, const void *ptr) {
    int id = object_index(r, ptr);
    if (id < 0 || r->objects[id].state != 1 ||
        !r->objects[id].references ||
        r->objects[id].references == UINT32_MAX) return -1;
    ++r->objects[id].references;
    return 0;
}
int phase22d_retain(phase22d_runtime *r, const void *ptr) {
    int ok;
    if (!r || pthread_mutex_lock(&r->mutex)) return -1;
    ok = retain_locked(r, ptr);
    pthread_mutex_unlock(&r->mutex);
    return ok;
}
int phase22d_release(phase22d_runtime *r, const void *ptr) {
    int index;
    phase22d_deallocator dealloc = NULL;
    void *context = NULL, *address = NULL;
    if (!r || pthread_mutex_lock(&r->mutex)) return -1;
    index = object_index(r, ptr);
    if (index < 0 || r->objects[index].state != 1 ||
        !r->objects[index].references) {
        pthread_mutex_unlock(&r->mutex); return -1;
    }
    object_entry *o = &r->objects[index];
    ++r->metrics.releases;
    if (--o->references == 0) {
        o->state = 2;
        for (size_t i = 0; i < r->nweak; ++i)
            if (r->weak[i].object_index == index) {
                *r->weak[i].slot = NULL;
                r->weak[i].object_index = -1;
            }
        class_entry *c = &r->classes[o->type];
        dealloc = c->deallocator;
        context = c->context;
        address = o->address;
    }
    pthread_mutex_unlock(&r->mutex);
    if (address) {
        if (dealloc) dealloc(address, context);
        pthread_mutex_lock(&r->mutex);
        o->state = 3;
        r->live--;
        r->metrics.destroyed++;
        pthread_mutex_unlock(&r->mutex);
    }
    return 0;
}
static int weak_index(const phase22d_runtime *r, void **slot) {
    if (!slot) return -1;
    for (size_t i = 0; i < r->nweak; ++i)
        if (r->weak[i].slot == slot) return (int)i;
    return -1;
}
int phase22d_weak_store(phase22d_runtime *r, void **slot, void *ptr) {
    int id = -1, wi, ok = -1;
    if (!r || !slot || pthread_mutex_lock(&r->mutex)) return -1;
    if (ptr) {
        id = object_index(r, ptr);
        if (id < 0 || r->objects[id].state != 1) goto done;
    }
    wi = weak_index(r, slot);
    if (wi < 0) {
        if (r->nweak == PHASE22D_WEAK_LIMIT) goto done;
        wi = (int)r->nweak++;
        r->weak[wi].slot = slot;
    }
    r->weak[wi].object_index = id;
    *slot = ptr;
    ok = 0;
done:
    pthread_mutex_unlock(&r->mutex);
    return ok;
}
int phase22d_weak_load_retained(phase22d_runtime *r, void **slot, void **out) {
    int wi, ok = -1;
    if (!out) return -1;
    *out = NULL;
    if (!r || pthread_mutex_lock(&r->mutex)) return -1;
    wi = weak_index(r, slot);
    if (wi < 0) goto done;
    int id = r->weak[wi].object_index;
    if (id < 0) { ok = 0; goto done; }
    if (!retain_locked(r, r->objects[id].address)) {
        *out = r->objects[id].address;
        ok = 0;
    }
done:
    pthread_mutex_unlock(&r->mutex);
    return ok;
}
int phase22d_weak_unregister(phase22d_runtime *r, void **slot) {
    int wi;
    if (!r || pthread_mutex_lock(&r->mutex)) return -1;
    wi = weak_index(r, slot);
    if (wi >= 0) {
        *slot = NULL;
        r->weak[wi] = r->weak[--r->nweak];
    }
    pthread_mutex_unlock(&r->mutex);
    return wi < 0 ? -1 : 0;
}
static int initialize_locked(phase22d_runtime *r, int type) {
    if (r->init_state[type] == 2) return 0;
    /* A single process-wide initializer owner prevents cross-thread
     * class A / class B initializer deadlocks; owner may recursively send. */
    while (r->init_depth && !pthread_equal(r->init_owner, pthread_self())) {
        ++r->metrics.init_waits;
        if (pthread_cond_wait(&r->initialized, &r->mutex)) return -1;
        if (r->init_state[type] == 2) return 0;
    }
    if (r->init_state[type] == 1) return 0; /* owner recursion */
    int parent = r->classes[type].parent;
    if (parent >= 0 && initialize_locked(r, parent)) return -1;
    if (r->init_state[type] == 2) return 0;
    if (!r->init_depth) r->init_owner = pthread_self();
    ++r->init_depth;
    r->init_state[type] = 1;
    phase22d_initializer fn = r->classes[type].initializer;
    void *ctx = r->classes[type].context;
    void *cls = &r->classes[type].class_tag;
    pthread_mutex_unlock(&r->mutex);
    int result = fn ? fn(ctx, cls) : 0;
    pthread_mutex_lock(&r->mutex);
    r->init_state[type] = result ? 0 : 2;
    if (!result) ++r->metrics.initializations;
    --r->init_depth;
    if (!r->init_depth) pthread_cond_broadcast(&r->initialized);
    return result ? -1 : 0;
}
static int receiver_type(phase22d_runtime *r, const void *receiver,
                         int *type, int *class_method, int *object_id) {
    if (!receiver) return -1;
    for (size_t i = 0; i < r->nclasses; ++i) {
        if (receiver == (void *)&r->classes[i].class_tag ||
            receiver == (void *)&r->classes[i].metaclass_tag) {
            *type = (int)i;
            *class_method = 1;
            *object_id = -1;
            return 0;
        }
    }
    int index = object_index(r, receiver);
    if (index < 0 || r->objects[index].state != 1) return -1;
    *type = r->objects[index].type;
    *class_method = 0;
    *object_id = index;
    return 0;
}
static int find_method(phase22d_runtime *r, int type, int class_method,
                       int sel, const char *super, method_entry *out) {
    if (super) {
        int lexical = class_index(r->classes, r->nclasses, super), at = type;
        if (lexical < 0) return -1;
        while (at >= 0 && at != lexical) at = r->classes[at].parent;
        if (at < 0) return -1;
        type = r->classes[lexical].parent;
    }
    while (type >= 0) {
        class_entry *c = &r->classes[type];
        method_entry *m = class_method ? c->classes : c->instances;
        size_t n = class_method ? c->class_count : c->instance_count;
        for (size_t j = 0; j < n; ++j)
            if (m[j].selector == (uint16_t)sel) {
                *out = m[j]; return 0;
            }
        type = c->parent;
    }
    return -1;
}
static int begin_send(phase22d_runtime *r, void *receiver, phase22d_sel sel,
                      const char *super, method_entry *m, int *pin) {
    int type, meta, idx, s;
    if (receiver_type(r, receiver, &type, &meta, &idx) ||
        (s = selector_index(r, sel)) < 0 ||
        initialize_locked(r, type) ||
        find_method(r, type, meta, s, super, m)) {
        ++r->metrics.failed_sends;
        return -1;
    }
    if (idx >= 0 && retain_locked(r, receiver)) {
        ++r->metrics.failed_sends; return -1;
    }
    *pin = idx >= 0;
    ++r->metrics.sends;
    return 0;
}
int phase22d_send_words(phase22d_runtime *r, void *receiver, phase22d_sel sel,
                        const char *super, const uintptr_t *args, size_t nargs,
                        uintptr_t *out) {
    method_entry m;
    int pin = 0, ok = -1;
    if (!r || !out || nargs > 4 || (nargs && !args)) return -1;
    *out = 0;
    if (pthread_mutex_lock(&r->mutex)) return -1;
    if (begin_send(r, receiver, sel, super, &m, &pin)) {
        pthread_mutex_unlock(&r->mutex); return -1;
    }
    if ((unsigned)m.signature != (unsigned)PHASE22D_WORD0 + nargs) {
        ++r->metrics.failed_sends;
        pthread_mutex_unlock(&r->mutex);
        if (pin) (void)phase22d_release(r, receiver);
        return -1;
    }
    pthread_mutex_unlock(&r->mutex);
    switch (nargs) {
    case 0: *out = m.implementation.word0(receiver, sel); break;
    case 1: *out = m.implementation.word1(receiver, sel, args[0]); break;
    case 2: *out = m.implementation.word2(receiver, sel, args[0], args[1]); break;
    case 3: *out = m.implementation.word3(receiver, sel, args[0], args[1], args[2]); break;
    case 4: *out = m.implementation.word4(receiver, sel, args[0], args[1], args[2], args[3]); break;
    default: return -1;
    }
    ok = 0;
    if (pin && phase22d_release(r, receiver)) ok = -1;
    return ok;
}
int phase22d_send_double1(phase22d_runtime *r, void *receiver, phase22d_sel sel,
                          const char *super, double arg, double *out) {
    method_entry m;
    int pin = 0, ok = 0;
    if (!r || !out) return -1;
    *out = 0;
    if (pthread_mutex_lock(&r->mutex)) return -1;
    if (begin_send(r, receiver, sel, super, &m, &pin)) {
        pthread_mutex_unlock(&r->mutex); return -1;
    }
    if (m.signature != PHASE22D_DOUBLE1) {
        ++r->metrics.failed_sends;
        pthread_mutex_unlock(&r->mutex);
        if (pin) (void)phase22d_release(r, receiver);
        return -1;
    }
    pthread_mutex_unlock(&r->mutex);
    *out = m.implementation.double1(receiver, sel, arg);
    if (pin && phase22d_release(r, receiver)) ok = -1;
    return ok;
}
int phase22d_statistics(phase22d_runtime *r, phase22d_stats *out) {
    if (!r || !out || pthread_mutex_lock(&r->mutex)) return -1;
    *out = r->metrics;
    out->classes = r->nclasses;
    out->selectors = r->nselectors;
    out->lifetime_objects = r->nobjects;
    out->live_objects = r->live;
    out->retained_bytes = r->memory;
    out->weak_locations = r->nweak;
    pthread_mutex_unlock(&r->mutex);
    return 0;
}
int phase22d_validate(phase22d_runtime *r) {
    int ok = 0;
    if (!r || pthread_mutex_lock(&r->mutex)) return -1;
    size_t live = 0, bytes = 0;
    if (r->nclasses > PHASE22D_CLASSES ||
        r->nselectors > PHASE22D_SELECTORS ||
        r->nobjects > PHASE22D_OBJECTS ||
        r->nweak > PHASE22D_WEAK_LIMIT) ok = -1;
    for (size_t i = 0; i < r->nobjects; ++i) {
        const object_entry *o = &r->objects[i];
        if (!o->address || o->type >= r->nclasses ||
            o->bytes != r->classes[o->type].bytes) ok = -1;
        bytes += o->bytes;
        if (o->state == 1 || o->state == 2) {
            if ((o->state == 1 && !o->references) ||
                (o->state == 2 && o->references)) ok = -1;
            ++live;
        } else if (o->state != 3 || o->references) ok = -1;
    }
    for (size_t i = 0; i < r->nweak; ++i) {
        if (!r->weak[i].slot ||
            (r->weak[i].object_index >= 0 &&
             ((size_t)r->weak[i].object_index >= r->nobjects ||
              r->objects[r->weak[i].object_index].state != 1))) ok = -1;
        for (size_t j = 0; j < i; ++j)
            if (r->weak[i].slot == r->weak[j].slot) ok = -1;
    }
    if (live != r->live || bytes != r->memory ||
        r->live > PHASE22D_LIVE_LIMIT ||
        r->memory > PHASE22D_MEMORY_LIMIT) ok = -1;
    pthread_mutex_unlock(&r->mutex);
    return ok;
}
