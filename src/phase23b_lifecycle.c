#define _POSIX_C_SOURCE 200809L
#include "ios27/phase23b_lifecycle.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <time.h>

#define P23B_MH_MAGIC 0xfeedfacfu
#define P23B_CPU_ARM64 0x0100000cu
#define P23B_MH_DYLIB 6u
#define P23B_LC_SEGMENT_64 0x19u
#define P23B_VM_PROT_WRITE 2u
#define P23B_VM_PROT_EXEC 4u

static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t u64(const uint8_t *p) {
    return (uint64_t)u32(p) | ((uint64_t)u32(p + 4) << 32);
}
static int span(size_t off, uint64_t len, size_t total) {
    return off <= total && len <= (uint64_t)(total - off);
}
static int section_is(const uint8_t *p, const char *name) {
    size_t n = strlen(name);
    return n <= 16 && memcmp(p, name, n) == 0 &&
           (n == 16 || p[n] == 0);
}
int ios27_23b_parse(const uint8_t *file, size_t length,
                    ios27_23b_image *out) {
    ios27_23b_image tmp = {0};
    size_t cursor, end;
    unsigned seen_init = 0, seen_term = 0;
    if (!file || !out || length < 32 || u32(file) != P23B_MH_MAGIC ||
        u32(file + 4) != P23B_CPU_ARM64 || u32(file + 12) != P23B_MH_DYLIB ||
        u32(file + 16) > 64 || u32(file + 20) > 1048576 ||
        !span(32, u32(file + 20), length)) return ENOEXEC;
    cursor = 32;
    end = cursor + u32(file + 20);
    for (uint32_t c = 0; c < u32(file + 16); ++c) {
        if (!span(cursor, 8, end)) return ENOEXEC;
        uint32_t type = u32(file + cursor), cmdsize = u32(file + cursor + 4);
        if (cmdsize < 8 || (cmdsize & 7u) || !span(cursor, cmdsize, end))
            return ENOEXEC;
        if (type == P23B_LC_SEGMENT_64) {
            const uint8_t *seg = file + cursor;
            uint64_t vmaddr, vmsize, fileoff, filesize;
            uint32_t nsects, prot;
            if (cmdsize < 72) return ENOEXEC;
            vmaddr = u64(seg + 24); vmsize = u64(seg + 32);
            fileoff = u64(seg + 40); filesize = u64(seg + 48);
            prot = u32(seg + 60); nsects = u32(seg + 64);
            if (nsects > 128 || (uint64_t)72 + (uint64_t)nsects * 80 > cmdsize ||
                vmaddr > UINT64_MAX - vmsize || fileoff > SIZE_MAX ||
                !span((size_t)fileoff, filesize, length) || filesize > vmsize)
                return ENOEXEC;
            if ((prot & P23B_VM_PROT_EXEC) != 0) {
                if ((prot & P23B_VM_PROT_WRITE) != 0 || vmsize == 0 ||
                    tmp.exec_count == IOS27_23B_MAX_SEGMENTS) return ENOEXEC;
                for (size_t k = 0; k < tmp.exec_count; ++k)
                    if (vmaddr < tmp.exec[k].end &&
                        tmp.exec[k].start < vmaddr + vmsize) return ENOEXEC;
                tmp.exec[tmp.exec_count++] = (ios27_23b_exec_range){
                    vmaddr, vmaddr + vmsize};
            }
            for (uint32_t s = 0; s < nsects; ++s) {
                const uint8_t *sec = seg + 72 + 80u * s;
                int is_init = section_is(sec, "__mod_init_func");
                int is_term = section_is(sec, "__mod_term_func");
                uint64_t addr, bytes, offset;
                size_t count;
                if (!is_init && !is_term) continue;
                addr = u64(sec + 32); bytes = u64(sec + 40);
                offset = u32(sec + 48);
                if (bytes > IOS27_23B_MAX_CALLBACKS * 8u || (bytes & 7u) ||
                    (addr & 7u) || (offset & 7u) ||
                    addr < vmaddr || addr - vmaddr > vmsize ||
                    bytes > vmsize - (addr - vmaddr) ||
                    offset < fileoff || offset - fileoff > filesize ||
                    bytes > filesize - (offset - fileoff) ||
                    !span((size_t)offset, bytes, length)) return ENOEXEC;
                count = (size_t)(bytes / 8u);
                if (is_init) {
                    if (seen_init++) return ENOEXEC;
                    tmp.init_table = addr; tmp.init_count = count;
                } else {
                    if (seen_term++) return ENOEXEC;
                    tmp.term_table = addr; tmp.term_count = count;
                }
            }
        }
        cursor += cmdsize;
    }
    if (cursor != end || !tmp.exec_count) return ENOEXEC;
    *out = tmp;
    return 0;
}
static int valid_target(const ios27_23b_image *image, uint64_t target) {
    if (!target || (target & 3u)) return 0;
    for (size_t i = 0; i < image->exec_count; ++i)
        if (target >= image->exec[i].start &&
            target < image->exec[i].end &&
            image->exec[i].end - target >= 4) return 1;
    return 0;
}
int ios27_23b_bind(ios27_23b_image *image, ios27_23b_read_ptr reader,
                   void *context) {
    ios27_23b_image tmp;
    if (!image || !reader || image->init_count > IOS27_23B_MAX_CALLBACKS ||
        image->term_count > IOS27_23B_MAX_CALLBACKS ||
        image->exec_count > IOS27_23B_MAX_SEGMENTS || !image->exec_count)
        return EINVAL;
    tmp = *image;
    for (unsigned which = 0; which != 2; ++which) {
        size_t count = which ? tmp.term_count : tmp.init_count;
        uint64_t table = which ? tmp.term_table : tmp.init_table;
        uint64_t *dest = which ? tmp.term : tmp.init;
        if (count && (!table || table > UINT64_MAX - 8u * (count - 1)))
            return ENOEXEC;
        for (size_t i = 0; i < count; ++i) {
            uint64_t value = 0;
            if (reader(context, table + i * 8u, &value) ||
                !valid_target(&tmp, value)) return ENOEXEC;
            dest[i] = value;
        }
    }
    *image = tmp;
    return 0;
}
int ios27_23b_runtime_init(ios27_23b_runtime *r) {
    if (!r) return EINVAL;
    memset(r, 0, sizeof(*r));
    int e = pthread_mutex_init(&r->mutex, NULL);
    if (e) return e;
    e = pthread_cond_init(&r->changed, NULL);
    if (e) { (void)pthread_mutex_destroy(&r->mutex); return e; }
    r->initialized = 1;
    return 0;
}
int ios27_23b_add(ios27_23b_runtime *r, unsigned id,
                  const ios27_23b_image *image, const unsigned *deps,
                  size_t count) {
    if (!r || !r->initialized || !image || id >= IOS27_23B_MAX_IMAGES ||
        count > IOS27_23B_MAX_DEPS || (count && !deps) ||
        image->init_count > IOS27_23B_MAX_CALLBACKS ||
        image->term_count > IOS27_23B_MAX_CALLBACKS ||
        image->exec_count > IOS27_23B_MAX_SEGMENTS) return EINVAL;
    for (size_t i = 0; i < count; ++i)
        if (deps[i] >= IOS27_23B_MAX_IMAGES || deps[i] == id) return EINVAL;
    pthread_mutex_lock(&r->mutex);
    ios27_23b_slot *s = &r->slot[id];
    if (s->registered) { pthread_mutex_unlock(&r->mutex); return EEXIST; }
    s->image = *image;
    if (count) memcpy(s->deps, deps, count * sizeof(*deps));
    s->dep_count = count;
    s->registered = 1; s->generation = 1;
    if (r->count <= id) r->count = (size_t)id + 1;
    pthread_mutex_unlock(&r->mutex);
    return 0;
}
/* This DFS also calculates the dependency-first order and prevents cycles.
   It is only called under the runtime lock. */
static int visit(ios27_23b_runtime *r, unsigned id, unsigned char *mark,
                 unsigned *order, size_t *used) {
    if (id >= r->count || !r->slot[id].registered) return ENOENT;
    if (mark[id] == 1) return ELOOP;
    if (mark[id] == 2) return 0;
    mark[id] = 1;
    ios27_23b_slot *s = &r->slot[id];
    for (size_t i = 0; i < s->dep_count; ++i) {
        int e = visit(r, s->deps[i], mark, order, used);
        if (e) return e;
    }
    mark[id] = 2;
    order[(*used)++] = id;
    return 0;
}
static int graph(ios27_23b_runtime *r, unsigned id,
                 unsigned *order, size_t *used) {
    unsigned char mark[IOS27_23B_MAX_IMAGES] = {0};
    *used = 0;
    return visit(r, id, mark, order, used);
}
int ios27_23b_open(ios27_23b_runtime *r, unsigned id,
                   ios27_23b_handle *out) {
    unsigned order[IOS27_23B_MAX_IMAGES]; size_t n;
    if (!r || !r->initialized || !out) return EINVAL;
    pthread_mutex_lock(&r->mutex);
    int e = graph(r, id, order, &n);
    if (!e) for (size_t i = 0; i < n; ++i) {
        ios27_23b_slot *s = &r->slot[order[i]];
        if (s->refs == IOS27_23B_MAX_REFS) e = EOVERFLOW;
        if (s->state == IOS27_23B_STOPPING) e = EBUSY;
    }
    if (!e) {
        for (size_t i = 0; i < n; ++i) ++r->slot[order[i]].refs;
        *out = (ios27_23b_handle){id, r->slot[id].generation};
    }
    pthread_mutex_unlock(&r->mutex);
    return e;
}
static int token_ok(ios27_23b_runtime *r, ios27_23b_handle h) {
    return h.index < r->count && r->slot[h.index].registered &&
           h.generation == r->slot[h.index].generation &&
           r->slot[h.index].refs > 0;
}
static int await_slot(ios27_23b_runtime *r, ios27_23b_slot *s) {
    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline)) return errno;
    deadline.tv_sec += 3;
    while (s->state == IOS27_23B_STARTING) {
        if (pthread_equal(s->owner, pthread_self())) return EDEADLK;
        int e = pthread_cond_timedwait(&r->changed, &r->mutex, &deadline);
        if (e) return e;
    }
    return 0;
}
static int ensure_one(ios27_23b_runtime *r, unsigned id,
                      ios27_23b_dispatch fn, void *ctx) {
    ios27_23b_slot *s = &r->slot[id];
    pthread_mutex_lock(&r->mutex);
    if (!s->refs) { pthread_mutex_unlock(&r->mutex); return ESTALE; }
    int e = await_slot(r, s);
    if (e) { pthread_mutex_unlock(&r->mutex); return e; }
    if (s->state == IOS27_23B_READY) { pthread_mutex_unlock(&r->mutex); return 0; }
    if (s->state == IOS27_23B_FAILED) {
        e = s->failure;
        pthread_mutex_unlock(&r->mutex); return e;
    }
    if (s->state != IOS27_23B_NEW) {
        pthread_mutex_unlock(&r->mutex); return EBUSY;
    }
    /* Validate every callback before side effects; never invoke unknown VM. */
    for (size_t i = 0; i < s->image.init_count; ++i)
        if (!valid_target(&s->image, s->image.init[i])) {
            pthread_mutex_unlock(&r->mutex); return ENOEXEC;
        }
    for (size_t i = 0; i < s->image.term_count; ++i)
        if (!valid_target(&s->image, s->image.term[i])) {
            pthread_mutex_unlock(&r->mutex); return ENOEXEC;
        }
    s->state = IOS27_23B_STARTING;
    s->owner = pthread_self();
    pthread_mutex_unlock(&r->mutex);
    size_t completed = 0;
    for (; completed < s->image.init_count; ++completed) {
        pthread_mutex_lock(&r->mutex); ++s->active;
        pthread_mutex_unlock(&r->mutex);
        e = fn(ctx, id, IOS27_23B_CONSTRUCT, s->image.init[completed], completed);
        pthread_mutex_lock(&r->mutex); --s->active;
        pthread_cond_broadcast(&r->changed);
        pthread_mutex_unlock(&r->mutex);
        if (e) break;
    }
    if (e) {
        /* Only explicitly reversible fixtures may implement ROLLBACK.
           Best-effort compensate each successfully completed constructor. */
        for (size_t k = completed; k > 0; --k) {
            pthread_mutex_lock(&r->mutex); ++s->active;
            pthread_mutex_unlock(&r->mutex);
            (void)fn(ctx, id, IOS27_23B_ROLLBACK, s->image.init[k - 1], k - 1);
            pthread_mutex_lock(&r->mutex); --s->active;
            pthread_cond_broadcast(&r->changed);
            pthread_mutex_unlock(&r->mutex);
        }
    }
    pthread_mutex_lock(&r->mutex);
    s->failure = e;
    s->state = e ? IOS27_23B_FAILED : IOS27_23B_READY;
    pthread_cond_broadcast(&r->changed);
    pthread_mutex_unlock(&r->mutex);
    return e;
}
int ios27_23b_ensure(ios27_23b_runtime *r, ios27_23b_handle handle,
                     ios27_23b_dispatch fn, void *ctx) {
    unsigned order[IOS27_23B_MAX_IMAGES]; size_t n;
    if (!r || !r->initialized || !fn) return EINVAL;
    pthread_mutex_lock(&r->mutex);
    int e = token_ok(r, handle) ? graph(r, handle.index, order, &n) : ESTALE;
    pthread_mutex_unlock(&r->mutex);
    if (e) return e;
    for (size_t i = 0; i < n; ++i) {
        e = ensure_one(r, order[i], fn, ctx);
        if (e) return e;
    }
    return 0;
}
int ios27_23b_close(ios27_23b_runtime *r, ios27_23b_handle handle,
                    ios27_23b_dispatch fn, void *ctx) {
    unsigned order[IOS27_23B_MAX_IMAGES]; size_t n;
    if (!r || !r->initialized || !fn) return EINVAL;
    pthread_mutex_lock(&r->mutex);
    int e = token_ok(r, handle) ? graph(r, handle.index, order, &n) : ESTALE;
    if (!e) for (size_t i = 0; i < n; ++i) {
        ios27_23b_slot *s = &r->slot[order[i]];
        if (!s->refs) e = ESTALE;
        if (s->active || s->state == IOS27_23B_STARTING ||
            s->state == IOS27_23B_STOPPING) e = EBUSY;
    }
    if (e) { pthread_mutex_unlock(&r->mutex); return e; }
    for (size_t i = 0; i < n; ++i) --r->slot[order[i]].refs;
    for (size_t i = 0; i < n; ++i) {
        ios27_23b_slot *s = &r->slot[order[i]];
        if (!s->refs && s->state == IOS27_23B_READY)
            s->state = IOS27_23B_STOPPING;
    }
    pthread_mutex_unlock(&r->mutex);
    /* Reverse topological order: consumers finish before providers. */
    int first = 0;
    for (size_t i = n; i > 0; --i) {
        unsigned id = order[i - 1]; ios27_23b_slot *s = &r->slot[id];
        pthread_mutex_lock(&r->mutex);
        int stopping = s->state == IOS27_23B_STOPPING && !s->refs;
        pthread_mutex_unlock(&r->mutex);
        if (stopping) for (size_t k = s->image.term_count; k > 0; --k) {
            pthread_mutex_lock(&r->mutex); ++s->active;
            pthread_mutex_unlock(&r->mutex);
            int result = fn(ctx, id, IOS27_23B_DESTRUCT, s->image.term[k - 1], k - 1);
            if (result && !first) first = result;
            pthread_mutex_lock(&r->mutex); --s->active;
            pthread_cond_broadcast(&r->changed);
            pthread_mutex_unlock(&r->mutex);
        }
        pthread_mutex_lock(&r->mutex);
        if (!s->refs) {
            s->state = IOS27_23B_NEW;
            s->failure = 0;
            ++s->generation;
            if (!s->generation) ++s->generation;
        }
        pthread_cond_broadcast(&r->changed);
        pthread_mutex_unlock(&r->mutex);
    }
    return first;
}
int ios27_23b_snapshot(ios27_23b_runtime *r, unsigned id,
                       ios27_23b_state *state, unsigned *refs,
                       unsigned *active) {
    if (!r || !r->initialized || id >= IOS27_23B_MAX_IMAGES) return EINVAL;
    pthread_mutex_lock(&r->mutex);
    if (!r->slot[id].registered) {
        pthread_mutex_unlock(&r->mutex); return ENOENT;
    }
    if (state) *state = r->slot[id].state;
    if (refs) *refs = r->slot[id].refs;
    if (active) *active = r->slot[id].active;
    pthread_mutex_unlock(&r->mutex);
    return 0;
}
int ios27_23b_runtime_destroy(ios27_23b_runtime *r) {
    if (!r || !r->initialized) return EINVAL;
    pthread_mutex_lock(&r->mutex);
    for (size_t i = 0; i < r->count; ++i)
        if (r->slot[i].refs || r->slot[i].active ||
            r->slot[i].state != IOS27_23B_NEW) {
            pthread_mutex_unlock(&r->mutex); return EBUSY;
        }
    pthread_mutex_unlock(&r->mutex);
    int e = pthread_cond_destroy(&r->changed);
    if (e) return e;
    e = pthread_mutex_destroy(&r->mutex);
    if (!e) r->initialized = 0;
    return e;
}
