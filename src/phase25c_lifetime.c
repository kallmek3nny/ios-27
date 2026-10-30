#define _POSIX_C_SOURCE 200809L
#include "ios27/phase25c_lifetime.h"

#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_OBJECTS = 4096, MAX_WEAK = 4096, MAX_BYTES = 16 * 1024 * 1024,
       MAX_OBJECT_BYTES = 65536, MAX_POOLS = 64, MAX_PENDING = 4096 };
enum { LIVE = 1, TERMINAL = 2, FINALIZING = 3, DEAD = 4 };

typedef struct {
    void *address;
    const void *isa;
    uint64_t image_generation, object_generation;
    size_t bytes;
    uint32_t references, pins;
    unsigned char state;
    ios27_phase25c_dealloc destructor;
    void *context;
} object_entry;
typedef struct {
    void **location;
    void *object;
    uint64_t generation;
} weak_entry;
typedef struct pool_cookie {
    struct pool_cookie *next;
} pool_cookie;
typedef struct {
    pool_cookie *cookie;
    size_t mark;
} pool_entry;
typedef struct thread_ctx {
    struct thread_ctx *next;
    struct ios27_phase25c_runtime *runtime;
    size_t depth, pending_used;
    pool_entry pools[MAX_POOLS];
    void *pending[MAX_PENDING];
    int attached;
} thread_ctx;

struct ios27_phase25c_runtime {
    pthread_mutex_t lock;
    pthread_key_t key;
    object_entry objects[MAX_OBJECTS];
    weak_entry weak[MAX_WEAK];
    size_t used, weak_used, committed_bytes, live_objects, live_bytes;
    size_t threads_attached;
    int64_t fail_after;
    uint64_t generation;
    ios27_phase25c_stats stats;
    thread_ctx *threads;
    pool_cookie *cookies;
};
static _Thread_local ios27_phase25c_runtime *scope_runtime;

static object_entry *find_locked(ios27_phase25c_runtime *rt, const void *p) {
    if (!p) return NULL;
    for (size_t i = rt->used; i > 0; --i)
        if (rt->objects[i - 1].address == p) return &rt->objects[i - 1];
    return NULL;
}
static size_t weak_index(ios27_phase25c_runtime *rt, void **slot) {
    for (size_t i = 0; i < rt->weak_used; ++i)
        if (rt->weak[i].location == slot) return i;
    return SIZE_MAX;
}
static void zero_weak_locked(ios27_phase25c_runtime *rt, object_entry *e) {
    for (size_t i = 0; i < rt->weak_used; ++i) {
        weak_entry *w = &rt->weak[i];
        if (w->object == e->address && w->generation == e->object_generation) {
            *w->location = NULL;
            w->object = NULL;
            w->generation = 0;
            ++rt->stats.weak_zeroed;
        }
    }
}
static void finalize(ios27_phase25c_runtime *rt, object_entry *e) {
    /* Never hold the registry lock across user dealloc: it may release ivars
     * or invoke arbitrary supported methods. FINALIZING blocks resurrection. */
    if (e->destructor) e->destructor(rt, e->address, e->context);
    pthread_mutex_lock(&rt->lock);
    if (e->state != FINALIZING || e->references || e->pins) abort();
    e->state = DEAD;
    --rt->live_objects;
    rt->live_bytes -= e->bytes;
    ++rt->stats.deallocations;
    pthread_mutex_unlock(&rt->lock);
}
static void thread_exit(void *argument);

int ios27_phase25c_create(ios27_phase25c_runtime **out) {
    if (!out) return -1;
    *out = NULL;
    ios27_phase25c_runtime *rt = calloc(1, sizeof(*rt));
    if (!rt) return -1;
    if (pthread_mutex_init(&rt->lock, NULL)) { free(rt); return -1; }
    if (pthread_key_create(&rt->key, thread_exit)) {
        pthread_mutex_destroy(&rt->lock);
        free(rt);
        return -1;
    }
    rt->generation = 1;
    rt->fail_after = -1;
    *out = rt;
    return 0;
}
int ios27_phase25c_destroy(ios27_phase25c_runtime **ptr) {
    if (!ptr || !*ptr) return -1;
    ios27_phase25c_runtime *rt = *ptr;
    pthread_mutex_lock(&rt->lock);
    if (rt->live_objects || rt->weak_used || rt->threads_attached) {
        pthread_mutex_unlock(&rt->lock);
        return -1;
    }
    pthread_mutex_unlock(&rt->lock);
    if (scope_runtime == rt) return -1;
    pthread_key_delete(rt->key);
    for (size_t i = 0; i < rt->used; ++i) free(rt->objects[i].address);
    thread_ctx *ctx = rt->threads;
    while (ctx) { thread_ctx *next = ctx->next; free(ctx); ctx = next; }
    pool_cookie *cookie = rt->cookies;
    while (cookie) { pool_cookie *next = cookie->next; free(cookie); cookie = next; }
    pthread_mutex_destroy(&rt->lock);
    free(rt);
    *ptr = NULL;
    return 0;
}
void ios27_phase25c_fail_alloc_after(ios27_phase25c_runtime *rt,
                                      int64_t countdown) {
    if (!rt) return;
    pthread_mutex_lock(&rt->lock);
    rt->fail_after = countdown;
    pthread_mutex_unlock(&rt->lock);
}
int ios27_phase25c_alloc(ios27_phase25c_runtime *rt,
                         const ios27_phase25c_class *c,
                         ios27_phase25c_dealloc dealloc,
                         void *context, void **out) {
    if (!out) return -1;
    *out = NULL;
    if (!rt || !c || !c->isa || !c->image_generation ||
        c->instance_size < sizeof(void *) || c->instance_size > MAX_OBJECT_BYTES ||
        c->alignment < sizeof(void *) || c->alignment > 4096 ||
        (c->alignment & (c->alignment - 1)) ||
        c->ivar_offset > c->instance_size ||
        c->ivar_bytes > c->instance_size - c->ivar_offset ||
        (c->ivar_bytes && c->ivar_offset < sizeof(void *))) return -1;
    pthread_mutex_lock(&rt->lock);
    if (rt->used >= MAX_OBJECTS ||
        c->instance_size > MAX_BYTES - rt->committed_bytes ||
        rt->fail_after == 0) {
        pthread_mutex_unlock(&rt->lock);
        return -1;
    }
    if (rt->fail_after > 0) --rt->fail_after;
    void *p = NULL;
    int rc = posix_memalign(&p, c->alignment, c->instance_size);
    if (rc) { pthread_mutex_unlock(&rt->lock); return -1; }
    memset(p, 0, c->instance_size);
    memcpy(p, &c->isa, sizeof(c->isa));
    object_entry *e = &rt->objects[rt->used++];
    *e = (object_entry){ .address = p, .isa = c->isa,
        .image_generation = c->image_generation, .object_generation = rt->generation++,
        .bytes = c->instance_size, .references = 1, .state = LIVE,
        .destructor = dealloc, .context = context };
    rt->committed_bytes += c->instance_size;
    ++rt->live_objects;
    rt->live_bytes += c->instance_size;
    ++rt->stats.allocations;
    pthread_mutex_unlock(&rt->lock);
    *out = p;
    return 0;
}
int ios27_phase25c_identity(ios27_phase25c_runtime *rt, const void *p,
                            const void *isa, uint64_t generation) {
    if (!rt || !p || !isa || !generation) return -1;
    pthread_mutex_lock(&rt->lock);
    object_entry *e = find_locked(rt, p);
    int valid = e && e->state != DEAD && e->isa == isa &&
                e->image_generation == generation &&
                memcmp(e->address, &isa, sizeof(isa)) == 0;
    pthread_mutex_unlock(&rt->lock);
    return valid ? 0 : -1;
}
int ios27_phase25c_can_unload(ios27_phase25c_runtime *rt,
                               const void *isa, uint64_t generation) {
    if (!rt || !isa || !generation) return -1;
    pthread_mutex_lock(&rt->lock);
    int permitted = 0;
    for (size_t i = 0; i < rt->used; ++i) {
        object_entry *e = &rt->objects[i];
        if (e->isa == isa && e->image_generation == generation && e->state != DEAD) {
            permitted = -1;
            break;
        }
    }
    pthread_mutex_unlock(&rt->lock);
    return permitted;
}
int ios27_phase25c_retain(ios27_phase25c_runtime *rt, const void *p) {
    if (!rt) return -1;
    if (!p) return 0;
    pthread_mutex_lock(&rt->lock);
    object_entry *e = find_locked(rt, p);
    int ok = e && e->state == LIVE && e->references &&
             e->references < UINT32_MAX;
    if (ok) { ++e->references; ++rt->stats.transitions; }
    pthread_mutex_unlock(&rt->lock);
    return ok ? 0 : -1;
}
int ios27_phase25c_retain_count(ios27_phase25c_runtime *rt, const void *p,
                                uint32_t *out) {
    if (!rt || !p || !out) return -1;
    pthread_mutex_lock(&rt->lock);
    object_entry *e = find_locked(rt, p);
    int ok = e && e->state == LIVE;
    if (ok) *out = e->references;
    pthread_mutex_unlock(&rt->lock);
    return ok ? 0 : -1;
}
int ios27_phase25c_release(ios27_phase25c_runtime *rt, const void *p) {
    if (!rt) return -1;
    if (!p) return 0;
    pthread_mutex_lock(&rt->lock);
    object_entry *e = find_locked(rt, p);
    if (!e || e->state != LIVE || !e->references) {
        pthread_mutex_unlock(&rt->lock);
        return -1;
    }
    --e->references;
    ++rt->stats.transitions;
    int finish = 0;
    if (!e->references) {
        /* Weak observations become nil before any dealloc/user callback. */
        zero_weak_locked(rt, e);
        e->state = e->pins ? TERMINAL : FINALIZING;
        finish = !e->pins;
    }
    pthread_mutex_unlock(&rt->lock);
    if (finish) finalize(rt, e);
    return 0;
}
int ios27_phase25c_pin(ios27_phase25c_runtime *rt, const void *p) {
    if (!rt) return -1;
    if (!p) return 0;
    pthread_mutex_lock(&rt->lock);
    object_entry *e = find_locked(rt, p);
    int ok = e && e->state == LIVE && e->pins < UINT32_MAX;
    if (ok) ++e->pins;
    pthread_mutex_unlock(&rt->lock);
    return ok ? 0 : -1;
}
int ios27_phase25c_unpin(ios27_phase25c_runtime *rt, const void *p) {
    if (!rt) return -1;
    if (!p) return 0;
    pthread_mutex_lock(&rt->lock);
    object_entry *e = find_locked(rt, p);
    if (!e || !e->pins || (e->state != LIVE && e->state != TERMINAL)) {
        pthread_mutex_unlock(&rt->lock);
        return -1;
    }
    --e->pins;
    int finish = e->state == TERMINAL && !e->pins;
    if (finish) e->state = FINALIZING;
    pthread_mutex_unlock(&rt->lock);
    if (finish) finalize(rt, e);
    return 0;
}
int ios27_phase25c_store_strong(ios27_phase25c_runtime *rt,
                                void **slot, void *next) {
    if (!rt || !slot) return -1;
    pthread_mutex_lock(&rt->lock);
    void *previous = *slot;
    object_entry *old = find_locked(rt, previous);
    object_entry *new_entry = find_locked(rt, next);
    if ((previous && (!old || old->state != LIVE)) ||
        (next && (!new_entry || new_entry->state != LIVE ||
                  new_entry->references == UINT32_MAX))) {
        pthread_mutex_unlock(&rt->lock);
        return -1;
    }
    if (previous == next) { pthread_mutex_unlock(&rt->lock); return 0; }
    if (next) { ++new_entry->references; ++rt->stats.transitions; }
    *slot = next;
    pthread_mutex_unlock(&rt->lock);
    /* The old slot's reference remains ours until this release; dealloc can
     * reenter this API safely because the registry lock is not held. */
    return ios27_phase25c_release(rt, previous);
}
int ios27_phase25c_store_weak(ios27_phase25c_runtime *rt,
                              void **slot, void *next) {
    if (!rt || !slot) return -1;
    pthread_mutex_lock(&rt->lock);
    object_entry *e = find_locked(rt, next);
    size_t i = weak_index(rt, slot);
    if ((next && (!e || e->state != LIVE)) ||
        (i == SIZE_MAX && rt->weak_used == MAX_WEAK)) {
        pthread_mutex_unlock(&rt->lock);
        return -1;
    }
    if (i == SIZE_MAX) i = rt->weak_used++;
    rt->weak[i] = (weak_entry){slot, next, e ? e->object_generation : 0};
    *slot = next;
    pthread_mutex_unlock(&rt->lock);
    return 0;
}
int ios27_phase25c_load_weak_retained(ios27_phase25c_runtime *rt,
                                      void **slot, void **out) {
    if (!out) return -1;
    *out = NULL;
    if (!rt || !slot) return -1;
    pthread_mutex_lock(&rt->lock);
    size_t i = weak_index(rt, slot);
    if (i == SIZE_MAX) { pthread_mutex_unlock(&rt->lock); return -1; }
    weak_entry *w = &rt->weak[i];
    if (w->object) {
        object_entry *e = find_locked(rt, w->object);
        if (!e || e->state != LIVE || !e->references ||
            e->references == UINT32_MAX || e->object_generation != w->generation) {
            pthread_mutex_unlock(&rt->lock);
            return -1;
        }
        ++e->references;
        ++rt->stats.transitions;
        *out = e->address;
    }
    ++rt->stats.weak_loads;
    pthread_mutex_unlock(&rt->lock);
    return 0;
}
int ios27_phase25c_destroy_weak(ios27_phase25c_runtime *rt, void **slot) {
    if (!rt || !slot) return -1;
    pthread_mutex_lock(&rt->lock);
    size_t i = weak_index(rt, slot);
    if (i == SIZE_MAX) { pthread_mutex_unlock(&rt->lock); return -1; }
    *slot = NULL;
    rt->weak[i] = rt->weak[--rt->weak_used];
    memset(&rt->weak[rt->weak_used], 0, sizeof(rt->weak[0]));
    pthread_mutex_unlock(&rt->lock);
    return 0;
}
static thread_ctx *get_thread(ios27_phase25c_runtime *rt, int create) {
    thread_ctx *ctx = pthread_getspecific(rt->key);
    if (ctx || !create) return ctx;
    ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    ctx->runtime = rt;
    ctx->attached = 1;
    if (pthread_setspecific(rt->key, ctx)) { free(ctx); return NULL; }
    pthread_mutex_lock(&rt->lock);
    ctx->next = rt->threads;
    rt->threads = ctx;
    ++rt->threads_attached;
    pthread_mutex_unlock(&rt->lock);
    return ctx;
}
int ios27_phase25c_pool_push(ios27_phase25c_runtime *rt, void **out) {
    if (!out) return -1;
    *out = NULL;
    if (!rt) return -1;
    thread_ctx *ctx = get_thread(rt, 1);
    if (!ctx || ctx->depth == MAX_POOLS) return -1;
    pool_cookie *cookie = malloc(sizeof(*cookie));
    if (!cookie) return -1;
    pthread_mutex_lock(&rt->lock);
    cookie->next = rt->cookies;
    rt->cookies = cookie;
    pthread_mutex_unlock(&rt->lock);
    ctx->pools[ctx->depth++] = (pool_entry){cookie, ctx->pending_used};
    *out = cookie;
    return 0;
}
int ios27_phase25c_autorelease(ios27_phase25c_runtime *rt, const void *p) {
    if (!rt) return -1;
    if (!p) return 0;
    thread_ctx *ctx = get_thread(rt, 0);
    if (!ctx || !ctx->depth || ctx->pending_used >= MAX_PENDING) return -1;
    pthread_mutex_lock(&rt->lock);
    object_entry *e = find_locked(rt, p);
    int ok = e && e->state == LIVE && e->references;
    if (ok) ctx->pending[ctx->pending_used++] = (void *)p;
    pthread_mutex_unlock(&rt->lock);
    return ok ? 0 : -1;
}
int ios27_phase25c_pool_pop(ios27_phase25c_runtime *rt, void *token) {
    if (!rt || !token) return -1;
    thread_ctx *ctx = get_thread(rt, 0);
    if (!ctx || !ctx->depth || ctx->pools[ctx->depth - 1].cookie != token)
        return -1;
    size_t mark = ctx->pools[ctx->depth - 1].mark;
    size_t count = ctx->pending_used - mark;
    void **drain = count ? malloc(count * sizeof(*drain)) : NULL;
    if (count && !drain) return -1;
    if (count) memcpy(drain, ctx->pending + mark, count * sizeof(*drain));
    memset(ctx->pending + mark, 0, count * sizeof(*ctx->pending));
    ctx->pending_used = mark;
    --ctx->depth;
    /* Pop first so nested callbacks autorelease into the outer pool. */
    int status = 0;
    for (size_t i = count; i > 0; --i)
        if (ios27_phase25c_release(rt, drain[i - 1])) status = -1;
    free(drain);
    return status;
}
int ios27_phase25c_thread_detach(ios27_phase25c_runtime *rt) {
    if (!rt) return -1;
    thread_ctx *ctx = get_thread(rt, 0);
    if (!ctx) return 0;
    while (ctx->depth) {
        if (ios27_phase25c_pool_pop(rt, ctx->pools[ctx->depth - 1].cookie))
            return -1;
    }
    if (ctx->pending_used || pthread_setspecific(rt->key, NULL)) return -1;
    pthread_mutex_lock(&rt->lock);
    if (!ctx->attached || !rt->threads_attached) abort();
    ctx->attached = 0;
    --rt->threads_attached;
    pthread_mutex_unlock(&rt->lock);
    return 0;
}
static void thread_exit(void *argument) {
    thread_ctx *ctx = argument;
    if (!ctx || !ctx->attached) return;
    /* pthread clears the key before calling its destructor. Restore it for
     * deterministic reverse-order ARC drain, then detach permanently. */
    if (pthread_setspecific(ctx->runtime->key, ctx) ||
        ios27_phase25c_thread_detach(ctx->runtime)) abort();
}
void ios27_phase25c_get_stats(ios27_phase25c_runtime *rt,
                              ios27_phase25c_stats *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!rt) return;
    pthread_mutex_lock(&rt->lock);
    *out = rt->stats;
    out->live_objects = rt->live_objects;
    out->live_bytes = rt->live_bytes;
    out->weak_slots = rt->weak_used;
    out->thread_contexts = rt->threads_attached;
    pthread_mutex_unlock(&rt->lock);
}
int ios27_phase25c_validate(ios27_phase25c_runtime *rt) {
    if (!rt) return -1;
    pthread_mutex_lock(&rt->lock);
    size_t live = 0, bytes = 0, committed = 0, dead = 0;
    int ok = rt->used <= MAX_OBJECTS && rt->weak_used <= MAX_WEAK;
    for (size_t i = 0; ok && i < rt->used; ++i) {
        object_entry *e = &rt->objects[i];
        committed += e->bytes;
        if (!e->address || e->bytes > MAX_OBJECT_BYTES ||
            !e->isa || !e->image_generation ||
            (e->state == LIVE && !e->references) ||
            (e->state != LIVE && e->references) ||
            (e->state == TERMINAL && !e->pins)) { ok = 0; break; }
        if (e->state == DEAD) ++dead;
        else if (e->state >= LIVE && e->state <= FINALIZING) {
            ++live; bytes += e->bytes;
        } else ok = 0;
    }
    for (size_t i = 0; ok && i < rt->weak_used; ++i) {
        weak_entry *w = &rt->weak[i];
        if (!w->location || *w->location != w->object) { ok = 0; break; }
        for (size_t j = 0; j < i; ++j)
            if (rt->weak[j].location == w->location) ok = 0;
        if (w->object) {
            object_entry *e = find_locked(rt, w->object);
            if (!e || e->state != LIVE || e->object_generation != w->generation)
                ok = 0;
        }
    }
    if (live != rt->live_objects || bytes != rt->live_bytes ||
        committed != rt->committed_bytes || committed > MAX_BYTES ||
        dead != rt->stats.deallocations) ok = 0;
    pthread_mutex_unlock(&rt->lock);
    return ok ? 0 : -1;
}
ios27_phase25c_runtime *ios27_phase25c_scope_enter(ios27_phase25c_runtime *rt) {
    ios27_phase25c_runtime *previous = scope_runtime;
    scope_runtime = rt;
    return previous;
}
void ios27_phase25c_scope_leave(ios27_phase25c_runtime *previous) {
    scope_runtime = previous;
}
static ios27_phase25c_runtime *bound(void) {
    if (!scope_runtime) { fputs("phase25c: unbound ARC entrypoint\n", stderr); abort(); }
    return scope_runtime;
}
static void require(int rc) {
    if (rc) { fputs("phase25c: invalid ARC ownership operation\n", stderr); abort(); }
}
void *objc_retain(void *p) {
    require(ios27_phase25c_retain(bound(), p));
    return p;
}
void objc_release(void *p) {
    require(ios27_phase25c_release(bound(), p));
}
void objc_storeStrong(void **slot, void *p) {
    require(ios27_phase25c_store_strong(bound(), slot, p));
}
void *objc_storeWeak(void **slot, void *p) {
    require(ios27_phase25c_store_weak(bound(), slot, p));
    return p;
}
void *objc_initWeak(void **slot, void *p) {
    if (!slot) abort();
    *slot = NULL;
    return objc_storeWeak(slot, p);
}
void *objc_loadWeakRetained(void **slot) {
    void *p = NULL;
    require(ios27_phase25c_load_weak_retained(bound(), slot, &p));
    return p;
}
void objc_destroyWeak(void **slot) {
    require(ios27_phase25c_destroy_weak(bound(), slot));
}
void *objc_autorelease(void *p) {
    require(ios27_phase25c_autorelease(bound(), p));
    return p;
}
void *objc_autoreleasePoolPush(void) {
    void *token = NULL;
    require(ios27_phase25c_pool_push(bound(), &token));
    return token;
}
void objc_autoreleasePoolPop(void *token) {
    require(ios27_phase25c_pool_pop(bound(), token));
}
