#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "ios27/phase25b_dispatch.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define CACHE_SIZE 256u
#define NONE UINT32_MAX

typedef struct {
    uint32_t selector, image;
    size_t offset;
    phase25b_abi abi;
} p25_method;
typedef struct {
    char name[PHASE25B_NAME_LENGTH];
    uint32_t parent;
    uint8_t class_tag, meta_tag;
    p25_method instances[PHASE25B_MAX_METHODS], classes[PHASE25B_MAX_METHODS];
    uint32_t ni, nc;
} p25_class;
typedef struct {
    void *mapping;
    size_t length, mapped;
    uint32_t pins;
    uint8_t live;
} p25_image;
typedef struct {
    void *pointer;
    uint32_t class_id, pins;
} p25_object;
typedef struct {
    uint32_t start, selector, owner, slot;
    uint8_t metaclass, valid;
    uint64_t epoch;
} p25_cache;
struct phase25b_runtime {
    pthread_mutex_t mutex;
    size_t nc, ns, ni, no, active;
    uint64_t epoch, hits, misses, sends, rejects, nil_sends;
    p25_class classes[PHASE25B_MAX_CLASSES];
    char selectors[PHASE25B_MAX_SELECTORS][PHASE25B_NAME_LENGTH];
    p25_image images[PHASE25B_MAX_IMAGES];
    p25_object objects[PHASE25B_MAX_OBJECTS];
    p25_cache cache[CACHE_SIZE];
};
static pthread_mutex_t global_mutex = PTHREAD_MUTEX_INITIALIZER;
static phase25b_runtime *active_runtime;
static _Thread_local phase25b_status last_status = PHASE25B_OK;

static int text_ok(const char *p) {
    if (!p) return 0;
    for (size_t i = 0; i < PHASE25B_NAME_LENGTH; ++i) {
        unsigned char c = (unsigned char)p[i];
        if (!c) return i != 0;
        if (c < 33 || c > 126) return 0;
    }
    return 0;
}
static int digits_at(const char **cursor, unsigned expected) {
    const char *p = *cursor;
    unsigned n = 0;
    if (*p < '0' || *p > '9') return -1;
    do {
        unsigned digit = (unsigned)(*p++ - '0');
        if (n > 4096u || n * 10u + digit > 4096u) return -1;
        n = n * 10u + digit;
    } while (*p >= '0' && *p <= '9');
    *cursor = p;
    return n == expected ? 0 : -1;
}
static int word_type(const char **cursor) {
    const char *p = *cursor;
    if (*p == '^' && p[1] == 'v') { *cursor += 2; return 1; }
    if (*p == 'Q' || *p == 'q' || *p == '@' || *p == '#' || *p == ':') {
        ++*cursor;
        return 1;
    }
    return 0;
}
phase25b_abi phase25b_parse_encoding(const char *s) {
    if (!text_ok(s)) return PHASE25B_UNSUPPORTED;
    const char *p = s;
    int ret = 0;
    if (*p == 'v') { ret = 1; ++p; }
    else if (*p == 'f') { ret = 2; ++p; }
    else if (*p == 'd') { ret = 3; ++p; }
    else if (word_type(&p)) ret = 4;
    else return PHASE25B_UNSUPPORTED;
    const char *size_begin = p;
    while (*p >= '0' && *p <= '9') ++p;
    /* The two hidden arguments always occupy the first 16 bytes in ObjC2. */
    unsigned total = 0;
    for (const char *q = size_begin; q != p; ++q) {
        if (total > 409u) return PHASE25B_UNSUPPORTED;
        total = total * 10u + (unsigned)(*q - '0');
    }
    if (p == size_begin || *p++ != '@' || digits_at(&p, 0) ||
        *p++ != ':' || digits_at(&p, 8)) return PHASE25B_UNSUPPORTED;
    unsigned args = 0;
    if (ret == 2 || ret == 3) {
        if (*p++ != (ret == 2 ? 'f' : 'd') || digits_at(&p, 16) ||
            *p || total != 24u) return PHASE25B_UNSUPPORTED;
        return ret == 2 ? PHASE25B_FLOAT1 : PHASE25B_DOUBLE1;
    }
    while (*p) {
        if (args >= 6 || !word_type(&p) || digits_at(&p, 16u + args * 8u))
            return PHASE25B_UNSUPPORTED;
        ++args;
    }
    if (total != 16u + 8u * args) return PHASE25B_UNSUPPORTED;
    if (ret == 1) return args ? PHASE25B_UNSUPPORTED : PHASE25B_VOID0;
    return (phase25b_abi)(PHASE25B_WORD0 + args);
}
static int class_id(phase25b_runtime *r, const char *name) {
    if (!text_ok(name)) return -1;
    for (size_t i = 0; i < r->nc; ++i)
        if (!strcmp(r->classes[i].name, name)) return (int)i;
    return -1;
}
static int sel_id(phase25b_runtime *r, phase25b_sel token) {
    for (size_t i = 0; i < r->ns; ++i)
        if (token == (phase25b_sel)r->selectors[i]) return (int)i;
    return -1;
}
static int intern(phase25b_runtime *r, const char *name) {
    if (!text_ok(name)) return -1;
    for (size_t i = 0; i < r->ns; ++i)
        if (!strcmp(r->selectors[i], name)) return (int)i;
    if (r->ns == PHASE25B_MAX_SELECTORS) return -1;
    size_t i = r->ns++;
    memcpy(r->selectors[i], name, strlen(name) + 1);
    return (int)i;
}
int phase25b_create(phase25b_runtime **out) {
    if (!out) return -1;
    *out = calloc(1, sizeof(**out));
    if (!*out) return -1;
    if (pthread_mutex_init(&(*out)->mutex, NULL)) {
        free(*out); *out = NULL; return -1;
    }
    (*out)->epoch = 1;
    return 0;
}
int phase25b_destroy(phase25b_runtime *r) {
    if (!r) return -1;
    pthread_mutex_lock(&global_mutex);
    if (active_runtime == r) {
        pthread_mutex_unlock(&global_mutex); return -1;
    }
    pthread_mutex_lock(&r->mutex);
    if (r->active) {
        pthread_mutex_unlock(&r->mutex);
        pthread_mutex_unlock(&global_mutex); return -1;
    }
    for (size_t i = 0; i < r->no; ++i) free(r->objects[i].pointer);
    for (size_t i = 0; i < r->ni; ++i)
        if (r->images[i].mapping) munmap(r->images[i].mapping, r->images[i].mapped);
    pthread_mutex_unlock(&r->mutex);
    pthread_mutex_destroy(&r->mutex);
    pthread_mutex_unlock(&global_mutex);
    free(r);
    return 0;
}
int phase25b_publish_image(phase25b_runtime *r, const void *bytes,
                           size_t length, uint32_t *out) {
    if (!r || !bytes || !out || !length || length > 1048576u) return -1;
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0 || (size_t)page > 65536u ||
        length > SIZE_MAX - (size_t)page) return -1;
    size_t mapped = (length + (size_t)page - 1u) / (size_t)page * (size_t)page;
    void *p = mmap(NULL, mapped, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return -1;
    memcpy(p, bytes, length);
    if (mprotect(p, mapped, PROT_READ | PROT_EXEC)) {
        munmap(p, mapped); return -1;
    }
    pthread_mutex_lock(&r->mutex);
    if (r->ni == PHASE25B_MAX_IMAGES) {
        pthread_mutex_unlock(&r->mutex);
        munmap(p, mapped); return -1;
    }
    uint32_t id = (uint32_t)r->ni++;
    r->images[id] = (p25_image){p, length, mapped, 0u, 1u};
    *out = id;
    ++r->epoch;
    pthread_mutex_unlock(&r->mutex);
    return 0;
}
int phase25b_retire_image(phase25b_runtime *r, uint32_t image) {
    if (!r) return -1;
    pthread_mutex_lock(&r->mutex);
    int ok = -1;
    if (image < r->ni && r->images[image].live && !r->images[image].pins) {
        int referenced = 0;
        for (size_t i = 0; i < r->nc; ++i) {
            p25_class *c = &r->classes[i];
            for (size_t j = 0; j < c->ni; ++j)
                if (c->instances[j].image == image) referenced = 1;
            for (size_t j = 0; j < c->nc; ++j)
                if (c->classes[j].image == image) referenced = 1;
        }
        if (!referenced) {
            p25_image *im = &r->images[image];
            munmap(im->mapping, im->mapped);
            im->mapping = NULL;
            im->live = 0;
            ++r->epoch;
            ok = 0;
        }
    }
    pthread_mutex_unlock(&r->mutex);
    return ok;
}
int phase25b_register_class(phase25b_runtime *r, const char *name,
                            const char *parent) {
    if (!r || !text_ok(name) || (parent && !text_ok(parent))) return -1;
    pthread_mutex_lock(&r->mutex);
    int par = parent ? class_id(r, parent) : -1;
    if (r->nc == PHASE25B_MAX_CLASSES || class_id(r, name) >= 0 ||
        (parent && par < 0)) {
        pthread_mutex_unlock(&r->mutex); return -1;
    }
    p25_class *c = &r->classes[r->nc++];
    memcpy(c->name, name, strlen(name) + 1);
    c->parent = parent ? (uint32_t)par : NONE;
    ++r->epoch;
    pthread_mutex_unlock(&r->mutex);
    return 0;
}
int phase25b_class_token(phase25b_runtime *r, const char *name,
                         int meta, void **out) {
    if (!r || !out || (meta != 0 && meta != 1)) return -1;
    pthread_mutex_lock(&r->mutex);
    int i = class_id(r, name);
    if (i >= 0) *out = meta ? (void *)&r->classes[i].meta_tag :
                             (void *)&r->classes[i].class_tag;
    pthread_mutex_unlock(&r->mutex);
    return i < 0 ? -1 : 0;
}
int phase25b_selector(phase25b_runtime *r, const char *name,
                      phase25b_sel *out) {
    if (!r || !out) return -1;
    pthread_mutex_lock(&r->mutex);
    int i = intern(r, name);
    if (i >= 0) *out = r->selectors[i];
    pthread_mutex_unlock(&r->mutex);
    return i < 0 ? -1 : 0;
}
int phase25b_method(phase25b_runtime *r, const char *class_name, int meta,
                    const char *selector, const char *encoding,
                    uint32_t image, size_t offset, int replace) {
    phase25b_abi abi = phase25b_parse_encoding(encoding);
    if (!r || abi == PHASE25B_UNSUPPORTED ||
        (meta != 0 && meta != 1) || (replace != 0 && replace != 1) ||
        !text_ok(selector) || (offset & 3u)) return -1;
    pthread_mutex_lock(&r->mutex);
    int c = class_id(r, class_name);
    if (c < 0 || image >= r->ni || !r->images[image].live ||
        offset > r->images[image].length ||
        r->images[image].length - offset < 4) {
        pthread_mutex_unlock(&r->mutex); return -1;
    }
    int sel = intern(r, selector);
    if (sel < 0) { pthread_mutex_unlock(&r->mutex); return -1; }
    p25_class *cls = &r->classes[c];
    p25_method *methods = meta ? cls->classes : cls->instances;
    uint32_t *count = meta ? &cls->nc : &cls->ni;
    int found = -1;
    for (size_t i = 0; i < *count; ++i)
        if (methods[i].selector == (uint32_t)sel) { found = (int)i; break; }
    if ((replace && found < 0) || (!replace && found >= 0) ||
        (found < 0 && *count == PHASE25B_MAX_METHODS)) {
        pthread_mutex_unlock(&r->mutex); return -1;
    }
    size_t index = found >= 0 ? (size_t)found : (*count)++;
    methods[index] = (p25_method){(uint32_t)sel, image, offset, abi};
    ++r->epoch;
    pthread_mutex_unlock(&r->mutex);
    return 0;
}
void *phase25b_alloc(phase25b_runtime *r, const char *class_name) {
    if (!r) return NULL;
    pthread_mutex_lock(&r->mutex);
    int id = class_id(r, class_name);
    void *p = NULL;
    if (id >= 0 && r->no < PHASE25B_MAX_OBJECTS) {
        p = calloc(1, 32);
        if (p) {
            void *cls = &r->classes[id].class_tag;
            memcpy(p, &cls, sizeof(cls));
            r->objects[r->no++] = (p25_object){p, (uint32_t)id, 0};
        }
    }
    pthread_mutex_unlock(&r->mutex);
    return p;
}
int phase25b_release(phase25b_runtime *r, void *p) {
    if (!r || !p) return -1;
    pthread_mutex_lock(&r->mutex);
    int rc = -1;
    for (size_t i = 0; i < r->no; ++i)
        if (r->objects[i].pointer == p) {
            if (r->objects[i].pins) rc = -2;
            else {
                free(p);
                /* Preserve slot IDs of outstanding pinned objects. */
                r->objects[i].pointer = NULL;
                rc = 0;
            }
            break;
        }
    pthread_mutex_unlock(&r->mutex);
    return rc;
}
static size_t cache_hash(uint32_t c, uint32_t s, int meta) {
    return (size_t)(c * 131u + s * 17u + (uint32_t)meta * 257u) % CACHE_SIZE;
}
static void reject(phase25b_runtime *r, phase25b_status status) {
    last_status = status;
    ++r->rejects;
}
/* Call with global_mutex held to prevent active runtime replacement; take
 * r->mutex only after global_mutex. Method and receiver pins outlive both. */
void *phase25b_prepare(const void *input, phase25b_sel sel,
                       phase25b_ticket *ticket, int is_super) {
    if (!ticket) return NULL;
    memset(ticket, 0, sizeof(*ticket));
    last_status = PHASE25B_BAD_RECEIVER;
    pthread_mutex_lock(&global_mutex);
    phase25b_runtime *r = active_runtime;
    if (!r) { pthread_mutex_unlock(&global_mutex); return NULL; }
    pthread_mutex_lock(&r->mutex);
    int s = sel_id(r, sel);
    if (s < 0) {
        reject(r, PHASE25B_BAD_SELECTOR); goto done;
    }
    if (!input && !is_super) {
        int scalar = 0;
        for (size_t j = 0; j < r->nc; ++j) {
            p25_class *cls = &r->classes[j];
            for (size_t k = 0; k < cls->ni; ++k)
                if (cls->instances[k].selector == (uint32_t)s) scalar = 1;
            for (size_t k = 0; k < cls->nc; ++k)
                if (cls->classes[k].selector == (uint32_t)s) scalar = 1;
        }
        if (scalar) { ++r->nil_sends; last_status = PHASE25B_NIL; }
        else reject(r, PHASE25B_BAD_METHOD);
        goto done;
    }
    uint32_t type = NONE, object_slot = NONE;
    int meta = -1;
    const void *receiver = input;
    const void *lexical = NULL;
    if (is_super) {
        /* Trusted compiler-generated objc_super pointer must be readable.
         * Do not dereference unknown receiver/class pointers. */
        if (!input || ((uintptr_t)input & (_Alignof(phase25b_super)-1u))) {
            reject(r, PHASE25B_BAD_SUPER); goto done;
        }
        const phase25b_super *super = (const phase25b_super *)input;
        receiver = super->receiver;
        lexical = super->current_class;
    }
    for (size_t i = 0; i < r->nc; ++i)
        if (receiver == &r->classes[i].class_tag) {
            type = (uint32_t)i; meta = 1; break;
        }
    if (type == NONE) for (size_t i = 0; i < r->no; ++i)
        if (r->objects[i].pointer && receiver == r->objects[i].pointer) {
            type = r->objects[i].class_id;
            object_slot = (uint32_t)i;
            meta = 0;
            break;
        }
    if (type == NONE) { reject(r, PHASE25B_BAD_RECEIVER); goto done; }
    uint32_t start = type;
    if (is_super) {
        uint32_t lexical_id = NONE;
        for (size_t i = 0; i < r->nc; ++i)
            if (lexical == &r->classes[i].class_tag ||
                (meta && lexical == &r->classes[i].meta_tag)) {
                lexical_id = (uint32_t)i;
                break;
            }
        uint32_t cur = type;
        while (cur != NONE && cur != lexical_id) cur = r->classes[cur].parent;
        if (lexical_id == NONE || cur == NONE ||
            r->classes[lexical_id].parent == NONE) {
            reject(r, PHASE25B_BAD_SUPER); goto done;
        }
        start = r->classes[lexical_id].parent;
    }
    p25_cache *line = &r->cache[cache_hash(start, (uint32_t)s, meta)];
    uint32_t owner = NONE, slot = NONE;
    if (line->valid && line->epoch == r->epoch &&
        line->start == start && line->selector == (uint32_t)s &&
        line->metaclass == (uint8_t)meta) {
        owner = line->owner; slot = line->slot;
        ++r->hits;
    } else {
        ++r->misses;
        for (uint32_t cur = start; cur != NONE; cur = r->classes[cur].parent) {
            p25_class *cls = &r->classes[cur];
            p25_method *arr = meta ? cls->classes : cls->instances;
            size_t n = meta ? cls->nc : cls->ni;
            for (size_t k = 0; k < n; ++k)
                if (arr[k].selector == (uint32_t)s) {
                    owner = cur; slot = (uint32_t)k; break;
                }
            if (owner != NONE) break;
        }
        if (owner != NONE) *line = (p25_cache){start, (uint32_t)s, owner,
                                               slot, (uint8_t)meta, 1, r->epoch};
    }
    if (owner == NONE) { reject(r, PHASE25B_BAD_METHOD); goto done; }
    p25_class *cls = &r->classes[owner];
    p25_method *m = meta ? &cls->classes[slot] : &cls->instances[slot];
    if (m->abi == PHASE25B_UNSUPPORTED || m->image >= r->ni ||
        !r->images[m->image].live) { reject(r, PHASE25B_RETIRED); goto done; }
    p25_image *im = &r->images[m->image];
    ++im->pins;
    if (object_slot != NONE) ++r->objects[object_slot].pins;
    ++r->active;
    ++r->sends;
    ticket->receiver = (void *)receiver;
    ticket->runtime = r;
    ticket->image_slot_plus_one = m->image + 1u;
    ticket->object_slot_plus_one = object_slot == NONE ? 0u : object_slot + 1u;
    last_status = PHASE25B_OK;
    /* Executable image is mapped and sealed RX by this runtime. ARM64 Linux
     * and Darwin share code/data pointer size. ABI is an explicit restriction. */
    void *imp = (uint8_t *)im->mapping + m->offset;
    pthread_mutex_unlock(&r->mutex);
    pthread_mutex_unlock(&global_mutex);
    return imp;
done:
    pthread_mutex_unlock(&r->mutex);
    pthread_mutex_unlock(&global_mutex);
    return NULL;
}
void phase25b_finish(phase25b_ticket *ticket) {
    if (!ticket || !ticket->runtime || !ticket->image_slot_plus_one) return;
    phase25b_runtime *r = ticket->runtime;
    pthread_mutex_lock(&r->mutex);
    uint32_t img = ticket->image_slot_plus_one - 1u;
    if (img < r->ni && r->images[img].pins) --r->images[img].pins;
    if (ticket->object_slot_plus_one) {
        uint32_t slot = ticket->object_slot_plus_one - 1u;
        if (slot < r->no && r->objects[slot].pins) --r->objects[slot].pins;
    }
    if (r->active) --r->active;
    pthread_mutex_unlock(&r->mutex);
    memset(ticket, 0, sizeof(*ticket));
}
int phase25b_set_active(phase25b_runtime *r) {
    pthread_mutex_lock(&global_mutex);
    if (active_runtime) {
        pthread_mutex_lock(&active_runtime->mutex);
        int busy = active_runtime->active != 0;
        pthread_mutex_unlock(&active_runtime->mutex);
        if (busy) { pthread_mutex_unlock(&global_mutex); return -1; }
    }
    active_runtime = r;
    pthread_mutex_unlock(&global_mutex);
    return 0;
}
void phase25b_statistics(phase25b_runtime *r, phase25b_stats *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!r) return;
    pthread_mutex_lock(&r->mutex);
    *out = (phase25b_stats){r->hits, r->misses, r->epoch,
                            r->sends, r->rejects, r->nil_sends,
                            r->nc, r->ns, r->ni, 0, r->active};
    for (size_t i = 0; i < r->no; ++i)
        if (r->objects[i].pointer) ++out->live_objects;
    pthread_mutex_unlock(&r->mutex);
}
phase25b_status phase25b_last_status(void) { return last_status; }
