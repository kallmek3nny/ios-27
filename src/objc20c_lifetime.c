#include "ios27/objc20c_lifetime.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { OBJ_LIVE = 1, OBJ_DYING = 2, OBJ_DEAD = 3 };

typedef struct {
    void *address;
    size_t bytes;
    uint32_t references;
    unsigned char state;
    ios27_objc20c_destructor destructor;
    void *context;
} object_entry;

typedef struct {
    void **location;
    const void *object;
} weak_entry;

typedef struct {
    size_t mark;
    uint64_t token;
} pool_entry;

struct ios27_objc20c_runtime {
    object_entry objects[IOS27_OBJC20C_MAX_OBJECTS];
    weak_entry weak[IOS27_OBJC20C_MAX_WEAK];
    pool_entry pools[IOS27_OBJC20C_MAX_POOL_DEPTH];
    const void *pending[IOS27_OBJC20C_MAX_AUTORELEASES];
    size_t used, weak_used, depth, pending_used;
    size_t live, live_bytes, allocated_bytes, destroyed;
    uint64_t next_pool_token;
};

static object_entry *find_object(const ios27_objc20c_runtime *rt,
                                 const void *ptr) {
    if (!rt || !ptr) return NULL;
    /* Includes tombstones so dead receivers cannot be accidentally accepted.
     * No allocation is freed/reused until runtime destruction. */
    for (size_t i = rt->used; i > 0; --i)
        if (rt->objects[i - 1].address == ptr)
            return (object_entry *)&rt->objects[i - 1];
    return NULL;
}

static size_t find_weak(const ios27_objc20c_runtime *rt, void **location) {
    if (!rt || !location) return SIZE_MAX;
    for (size_t i = 0; i < rt->weak_used; ++i)
        if (rt->weak[i].location == location) return i;
    return SIZE_MAX;
}

int ios27_objc20c_create(ios27_objc20c_runtime **out) {
    if (!out) return -1;
    *out = calloc(1u, sizeof(**out));
    if (!*out) return -1;
    (*out)->next_pool_token = 1u;
    return 0;
}

int ios27_objc20c_alloc(ios27_objc20c_runtime *rt, size_t bytes,
                       ios27_objc20c_destructor destructor, void *context,
                       void **out) {
    object_entry *e;
    void *address;
    if (!out) return -1;
    *out = NULL;
    if (!rt || bytes < sizeof(void *) ||
        bytes > IOS27_OBJC20C_MAX_OBJECT_BYTES ||
        rt->used >= IOS27_OBJC20C_MAX_OBJECTS ||
        rt->live >= IOS27_OBJC20C_MAX_LIVE ||
        bytes > IOS27_OBJC20C_MAX_BYTES - rt->allocated_bytes)
        return -1;
    address = calloc(1u, bytes);
    if (!address) return -1;
    e = &rt->objects[rt->used++];
    e->address = address;
    e->bytes = bytes;
    e->references = 1u;
    e->state = OBJ_LIVE;
    e->destructor = destructor;
    e->context = context;
    rt->live++;
    rt->live_bytes += bytes;
    rt->allocated_bytes += bytes;
    *out = address;
    return 0;
}

int ios27_objc20c_retain(ios27_objc20c_runtime *rt, const void *object) {
    object_entry *e = find_object(rt, object);
    if (!e || e->state != OBJ_LIVE || !e->references ||
        e->references == UINT32_MAX) return -1;
    ++e->references;
    return 0;
}

int ios27_objc20c_retain_count(const ios27_objc20c_runtime *rt,
                              const void *object, uint32_t *count) {
    object_entry *e = find_object(rt, object);
    if (!count || !e || e->state != OBJ_LIVE) return -1;
    *count = e->references;
    return 0;
}

int ios27_objc20c_release(ios27_objc20c_runtime *rt, const void *object) {
    object_entry *e = find_object(rt, object);
    if (!e || e->state != OBJ_LIVE || !e->references) return -1;
    if (--e->references) return 0;
    e->state = OBJ_DYING;
    /* objc weak zeroing begins before user dealloc callbacks; retaining a
     * dying receiver must fail, including from within its own destructor. */
    for (size_t i = 0; i < rt->weak_used; ++i) {
        if (rt->weak[i].object == object) {
            *rt->weak[i].location = NULL;
            rt->weak[i].object = NULL;
        }
    }
    if (e->destructor) e->destructor(rt, e->address, e->context);
    e->state = OBJ_DEAD;
    --rt->live;
    rt->live_bytes -= e->bytes;
    ++rt->destroyed;
    return 0;
}

int ios27_objc20c_strong_store(ios27_objc20c_runtime *rt, void **location,
                              void *next) {
    void *previous;
    object_entry *old_entry;
    if (!rt || !location) return -1;
    previous = *location;
    if (previous == next) {
        return !previous || (find_object(rt, previous) &&
               find_object(rt, previous)->state == OBJ_LIVE) ? 0 : -1;
    }
    old_entry = previous ? find_object(rt, previous) : NULL;
    if (previous && (!old_entry || old_entry->state != OBJ_LIVE)) return -1;
    if (next && ios27_objc20c_retain(rt, next)) return -1;
    *location = next;
    if (previous && ios27_objc20c_release(rt, previous)) {
        /* Cannot ordinarily fail after validation in a single-threaded
         * runtime, but preserve ownership if a custom destructor intervenes. */
        return -1;
    }
    return 0;
}

int ios27_objc20c_weak_store(ios27_objc20c_runtime *rt, void **location,
                            void *next) {
    size_t i;
    if (!rt || !location) return -1;
    if (next) {
        object_entry *e = find_object(rt, next);
        if (!e || e->state != OBJ_LIVE) return -1;
    }
    i = find_weak(rt, location);
    if (i == SIZE_MAX) {
        if (rt->weak_used >= IOS27_OBJC20C_MAX_WEAK) return -1;
        i = rt->weak_used++;
        rt->weak[i].location = location;
    }
    rt->weak[i].object = next;
    *location = next;
    return 0;
}

int ios27_objc20c_weak_unregister(ios27_objc20c_runtime *rt,
                                 void **location) {
    size_t i = find_weak(rt, location);
    if (i == SIZE_MAX) return -1;
    *location = NULL;
    --rt->weak_used;
    rt->weak[i] = rt->weak[rt->weak_used];
    memset(&rt->weak[rt->weak_used], 0, sizeof(rt->weak[rt->weak_used]));
    return 0;
}

int ios27_objc20c_weak_load_retained(ios27_objc20c_runtime *rt,
                                    void **location, void **out) {
    size_t i;
    if (!out) return -1;
    *out = NULL;
    i = find_weak(rt, location);
    if (i == SIZE_MAX) return -1;
    if (!rt->weak[i].object) return 0;
    if (ios27_objc20c_retain(rt, rt->weak[i].object)) return -1;
    *out = (void *)rt->weak[i].object;
    return 0;
}

int ios27_objc20c_pool_push(ios27_objc20c_runtime *rt, uint64_t *token) {
    pool_entry *p;
    if (!rt || !token || rt->depth == IOS27_OBJC20C_MAX_POOL_DEPTH ||
        rt->next_pool_token == UINT64_MAX) return -1;
    p = &rt->pools[rt->depth++];
    p->mark = rt->pending_used;
    p->token = rt->next_pool_token++;
    *token = p->token;
    return 0;
}

int ios27_objc20c_autorelease(ios27_objc20c_runtime *rt,
                             const void *object) {
    object_entry *e = find_object(rt, object);
    if (!rt || !rt->depth || !e || e->state != OBJ_LIVE ||
        rt->pending_used == IOS27_OBJC20C_MAX_AUTORELEASES) return -1;
    rt->pending[rt->pending_used++] = object;
    return 0;
}

int ios27_objc20c_pool_pop(ios27_objc20c_runtime *rt, uint64_t token) {
    const void **drain = NULL;
    size_t begin, count;
    int ok = 0;
    if (!rt || !rt->depth || rt->pools[rt->depth - 1].token != token)
        return -1;
    begin = rt->pools[rt->depth - 1].mark;
    count = rt->pending_used - begin;
    if (count) {
        drain = malloc(count * sizeof(*drain));
        if (!drain) return -1; /* No state changed; retry remains valid. */
        memcpy(drain, rt->pending + begin, count * sizeof(*drain));
        memset(rt->pending + begin, 0, count * sizeof(*drain));
    }
    --rt->depth;
    rt->pending_used = begin;
    /* Pool is already popped so callback autoreleases attach to its parent.
     * Copying the drain segment prevents callback appends from clobbering it. */
    for (size_t i = count; i > 0; --i)
        if (ios27_objc20c_release(rt, drain[i - 1])) ok = -1;
    free(drain);
    return ok;
}

void ios27_objc20c_get_stats(const ios27_objc20c_runtime *rt,
                            ios27_objc20c_stats *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!rt) return;
    out->live_objects = rt->live;
    out->live_bytes = rt->live_bytes;
    out->lifetime_objects = rt->used;
    out->lifetime_bytes = rt->allocated_bytes;
    out->weak_slots = rt->weak_used;
    out->pool_depth = rt->depth;
    out->pending_autoreleases = rt->pending_used;
    out->destroyed_objects = rt->destroyed;
}

int ios27_objc20c_validate(const ios27_objc20c_runtime *rt) {
    size_t live = 0u, bytes = 0u, allocated = 0u, dead = 0u;
    if (!rt || rt->used > IOS27_OBJC20C_MAX_OBJECTS ||
        rt->live > IOS27_OBJC20C_MAX_LIVE ||
        rt->weak_used > IOS27_OBJC20C_MAX_WEAK ||
        rt->depth > IOS27_OBJC20C_MAX_POOL_DEPTH ||
        rt->pending_used > IOS27_OBJC20C_MAX_AUTORELEASES) return -1;
    for (size_t i = 0; i < rt->used; ++i) {
        const object_entry *e = &rt->objects[i];
        if (!e->address || e->bytes < sizeof(void *) ||
            e->bytes > IOS27_OBJC20C_MAX_OBJECT_BYTES) return -1;
        allocated += e->bytes;
        if (e->state == OBJ_LIVE || e->state == OBJ_DYING) {
            if (e->state == OBJ_LIVE && !e->references) return -1;
            if (e->state == OBJ_DYING && e->references) return -1;
            ++live;
            bytes += e->bytes;
        } else if (e->state == OBJ_DEAD) {
            if (e->references) return -1;
            ++dead;
        } else return -1;
    }
    if (live != rt->live || bytes != rt->live_bytes ||
        allocated != rt->allocated_bytes || dead != rt->destroyed ||
        allocated > IOS27_OBJC20C_MAX_BYTES) return -1;
    for (size_t i = 0; i < rt->weak_used; ++i) {
        const weak_entry *w = &rt->weak[i];
        if (!w->location || *w->location != w->object) return -1;
        if (w->object) {
            object_entry *e = find_object(rt, w->object);
            if (!e || e->state != OBJ_LIVE) return -1;
        }
        for (size_t j = 0; j < i; ++j)
            if (rt->weak[j].location == w->location) return -1;
    }
    for (size_t i = 0; i < rt->depth; ++i)
        if (!rt->pools[i].token || rt->pools[i].mark > rt->pending_used ||
            (i && rt->pools[i].mark < rt->pools[i - 1].mark)) return -1;
    return 0;
}

void ios27_objc20c_destroy(ios27_objc20c_runtime *rt) {
    if (!rt) return;
    /* Runtime shutdown forcibly reclaims remaining payloads (including
     * cycles). Unlike normal -release, it never calls user destructors:
     * graph teardown callbacks would otherwise access already-dead peers. */
    for (size_t i = 0; i < rt->weak_used; ++i)
        *rt->weak[i].location = NULL;
    for (size_t i = 0; i < rt->used; ++i) free(rt->objects[i].address);
    free(rt);
}
