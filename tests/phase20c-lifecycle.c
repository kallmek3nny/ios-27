#include "ios27/objc20c_lifetime.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    void *isa;
    long value;
    void *child;
} node;
typedef struct {
    unsigned destroyed;
    unsigned char order[256];
    unsigned length;
} trace;

typedef struct {
    trace *log;
    unsigned id;
} context;

static void deallocate(ios27_objc20c_runtime *rt, void *ptr, void *arg) {
    node *n = ptr;
    context *c = arg;
    uint32_t count = UINT32_MAX;
    ++c->log->destroyed;
    if (c->log->length < sizeof(c->log->order))
        c->log->order[c->log->length++] = (unsigned char)c->id;
    assert(ios27_objc20c_retain(rt, n) == -1);
    assert(ios27_objc20c_retain_count(rt, n, &count) == -1);
    /* A subclass dealloc runs before releasing its owned child. */
    assert(ios27_objc20c_strong_store(rt, &n->child, NULL) == 0);
}

static node *make_node(ios27_objc20c_runtime *rt, long value,
                       context *c) {
    void *p = NULL;
    assert(ios27_objc20c_alloc(rt, sizeof(node), deallocate, c, &p) == 0);
    node *n = p;
    assert(n->isa == NULL && n->child == NULL && n->value == 0);
    n->value = value;
    return n;
}

static void basic_and_graph(void) {
    ios27_objc20c_runtime *rt = NULL;
    trace log = {0};
    context a = {&log, 1}, b = {&log, 2}, c = {&log, 3};
    node *parent, *child, *leaf;
    void *weak = NULL, *loaded = (void *)(uintptr_t)1;
    ios27_objc20c_stats stats;
    uint32_t count;
    assert(ios27_objc20c_create(&rt) == 0 && rt);
    assert(ios27_objc20c_alloc(rt, 0, NULL, NULL, &loaded) == -1);
    assert(loaded == NULL);
    assert(ios27_objc20c_alloc(rt, IOS27_OBJC20C_MAX_OBJECT_BYTES + 1u,
                              NULL, NULL, &loaded) == -1);
    assert(ios27_objc20c_retain(rt, (void *)(uintptr_t)0x17) == -1);
    assert(ios27_objc20c_release(rt, NULL) == -1);
    parent = make_node(rt, 1, &a);
    child = make_node(rt, 2, &b);
    leaf = make_node(rt, 3, &c);
    assert(ios27_objc20c_strong_store(rt, &parent->child, child) == 0);
    assert(ios27_objc20c_strong_store(rt, &child->child, leaf) == 0);
    assert(ios27_objc20c_retain_count(rt, child, &count) == 0 && count == 2);
    assert(ios27_objc20c_weak_store(rt, &weak, child) == 0 && weak == child);
    assert(ios27_objc20c_weak_load_retained(rt, &weak, &loaded) == 0 &&
           loaded == child);
    assert(ios27_objc20c_retain_count(rt, child, &count) == 0 && count == 3);
    assert(ios27_objc20c_release(rt, loaded) == 0);
    assert(ios27_objc20c_strong_store(rt, &parent->child, child) == 0);
    assert(ios27_objc20c_strong_store(rt, &parent->child, (void *)0x23) == -1);
    assert(parent->child == child);
    assert(ios27_objc20c_release(rt, leaf) == 0);
    assert(ios27_objc20c_release(rt, child) == 0);
    assert(ios27_objc20c_release(rt, parent) == 0);
    assert(log.destroyed == 3 && log.length == 3);
    assert(log.order[0] == 1 && log.order[1] == 2 && log.order[2] == 3);
    assert(weak == NULL);
    assert(ios27_objc20c_weak_load_retained(rt, &weak, &loaded) == 0 &&
           loaded == NULL);
    assert(ios27_objc20c_retain(rt, child) == -1);
    assert(ios27_objc20c_release(rt, parent) == -1);
    assert(ios27_objc20c_weak_store(rt, &weak, child) == -1);
    assert(ios27_objc20c_weak_unregister(rt, &weak) == 0);
    assert(ios27_objc20c_weak_unregister(rt, &weak) == -1);
    ios27_objc20c_get_stats(rt, &stats);
    assert(stats.live_objects == 0 && stats.destroyed_objects == 3);
    assert(stats.live_bytes == 0 && stats.lifetime_objects == 3);
    assert(ios27_objc20c_validate(rt) == 0);
    ios27_objc20c_destroy(rt);
}

static void pools(void) {
    ios27_objc20c_runtime *rt = NULL;
    trace log = {0};
    context a = {&log, 1}, b = {&log, 2}, c = {&log, 3};
    node *first, *second, *third;
    uint64_t outer, inner;
    void *weak = NULL;
    ios27_objc20c_stats stats;
    assert(ios27_objc20c_create(&rt) == 0);
    first = make_node(rt, 1, &a);
    assert(ios27_objc20c_autorelease(rt, first) == -1);
    assert(ios27_objc20c_pool_push(rt, &outer) == 0);
    assert(ios27_objc20c_autorelease(rt, first) == 0);
    second = make_node(rt, 2, &b);
    assert(ios27_objc20c_pool_push(rt, &inner) == 0 && outer != inner);
    assert(ios27_objc20c_autorelease(rt, second) == 0);
    third = make_node(rt, 3, &c);
    assert(ios27_objc20c_autorelease(rt, third) == 0);
    assert(ios27_objc20c_pool_pop(rt, outer) == -1);
    assert(ios27_objc20c_weak_store(rt, &weak, second) == 0);
    assert(ios27_objc20c_pool_pop(rt, inner) == 0);
    assert(log.destroyed == 2 && log.order[0] == 3 && log.order[1] == 2);
    assert(weak == NULL);
    assert(ios27_objc20c_weak_unregister(rt, &weak) == 0);
    assert(ios27_objc20c_pool_pop(rt, inner) == -1);
    assert(ios27_objc20c_pool_pop(rt, outer) == 0);
    assert(log.destroyed == 3 && log.order[2] == 1);
    ios27_objc20c_get_stats(rt, &stats);
    assert(stats.pool_depth == 0 && stats.pending_autoreleases == 0);
    assert(ios27_objc20c_validate(rt) == 0);
    ios27_objc20c_destroy(rt);
}

static void limits(void) {
    ios27_objc20c_runtime *rt = NULL;
    trace log = {0};
    context c = {&log, 9};
    node *ptrs[IOS27_OBJC20C_MAX_LIVE];
    uint64_t tokens[IOS27_OBJC20C_MAX_POOL_DEPTH];
    void *weak[IOS27_OBJC20C_MAX_WEAK + 1u] = {0};
    ios27_objc20c_stats stats;
    assert(ios27_objc20c_create(&rt) == 0);
    for (size_t i = 0; i < IOS27_OBJC20C_MAX_LIVE; ++i)
        ptrs[i] = make_node(rt, (long)i, &c);
    void *none = (void *)1;
    assert(ios27_objc20c_alloc(rt, sizeof(node), NULL, NULL, &none) == -1 &&
           none == NULL);
    for (size_t i = 0; i < IOS27_OBJC20C_MAX_WEAK; ++i)
        assert(ios27_objc20c_weak_store(rt, &weak[i], ptrs[0]) == 0);
    assert(ios27_objc20c_weak_store(rt, &weak[IOS27_OBJC20C_MAX_WEAK],
                                   ptrs[0]) == -1);
    for (size_t i = 0; i < IOS27_OBJC20C_MAX_POOL_DEPTH; ++i)
        assert(ios27_objc20c_pool_push(rt, &tokens[i]) == 0);
    uint64_t ignored = 0;
    assert(ios27_objc20c_pool_push(rt, &ignored) == -1);
    assert(ios27_objc20c_pool_pop(rt, tokens[0]) == -1);
    for (size_t i = IOS27_OBJC20C_MAX_POOL_DEPTH; i > 0; --i)
        assert(ios27_objc20c_pool_pop(rt, tokens[i - 1]) == 0);
    for (size_t i = 0; i < IOS27_OBJC20C_MAX_WEAK; ++i)
        assert(ios27_objc20c_weak_unregister(rt, &weak[i]) == 0);
    for (size_t i = 0; i < IOS27_OBJC20C_MAX_LIVE; ++i)
        assert(ios27_objc20c_release(rt, ptrs[i]) == 0);
    assert(log.destroyed == IOS27_OBJC20C_MAX_LIVE);
    /* Quarantine ensures every stale pointer remains rejected, even across
     * thousands of allocation/deallocation cycles. */
    for (size_t i = IOS27_OBJC20C_MAX_LIVE;
         i < IOS27_OBJC20C_MAX_OBJECTS; ++i) {
        node *temp = make_node(rt, (long)i, &c);
        assert(ios27_objc20c_release(rt, temp) == 0);
    }
    assert(ios27_objc20c_alloc(rt, sizeof(node), NULL, NULL, &none) == -1);
    assert(ios27_objc20c_release(rt, ptrs[0]) == -1);
    ios27_objc20c_get_stats(rt, &stats);
    assert(stats.lifetime_objects == IOS27_OBJC20C_MAX_OBJECTS);
    assert(stats.live_objects == 0 && stats.destroyed_objects == stats.lifetime_objects);
    assert(ios27_objc20c_validate(rt) == 0);
    ios27_objc20c_destroy(rt);
}

static void overflow_and_drain(void) {
    ios27_objc20c_runtime *rt = NULL;
    uint64_t pool;
    void *p = NULL;
    ios27_objc20c_stats stats;
    assert(ios27_objc20c_create(&rt) == 0);
    assert(ios27_objc20c_alloc(rt, sizeof(node), NULL, NULL, &p) == 0);
    assert(ios27_objc20c_pool_push(rt, &pool) == 0);
    for (size_t i = 0; i < IOS27_OBJC20C_MAX_AUTORELEASES; ++i) {
        assert(ios27_objc20c_retain(rt, p) == 0);
        assert(ios27_objc20c_autorelease(rt, p) == 0);
    }
    assert(ios27_objc20c_autorelease(rt, p) == -1);
    assert(ios27_objc20c_pool_pop(rt, pool) == 0);
    uint32_t refs = 0;
    assert(ios27_objc20c_retain_count(rt, p, &refs) == 0 && refs == 1);
    assert(ios27_objc20c_release(rt, p) == 0);
    assert(ios27_objc20c_pool_push(rt, &pool) == 0);
    void *dead = NULL;
    assert(ios27_objc20c_alloc(rt, sizeof(node), NULL, NULL, &dead) == 0);
    assert(ios27_objc20c_autorelease(rt, dead) == 0);
    assert(ios27_objc20c_release(rt, dead) == 0);
    /* Misowned autorelease is detected, but the pool is still drained. */
    assert(ios27_objc20c_pool_pop(rt, pool) == -1);
    ios27_objc20c_get_stats(rt, &stats);
    assert(!stats.live_objects && !stats.pending_autoreleases && !stats.pool_depth);
    assert(ios27_objc20c_validate(rt) == 0);
    ios27_objc20c_destroy(rt);
}

static void randomized(void) {
    ios27_objc20c_runtime *rt = NULL;
    uint32_t state = 0x20c2026u;
    void *entries[128] = {0};
    void *weak[128] = {0};
    assert(ios27_objc20c_create(&rt) == 0);
    for (size_t iter = 0; iter < 10000; ++iter) {
        state = state * 1664525u + 1013904223u;
        size_t slot = (state >> 12u) & 127u;
        if (entries[slot] == NULL) {
            assert(ios27_objc20c_alloc(rt, sizeof(node), NULL, NULL,
                                       &entries[slot]) == 0);
            assert(ios27_objc20c_weak_store(rt, &weak[slot], entries[slot]) == 0);
        } else {
            assert(ios27_objc20c_release(rt, entries[slot]) == 0);
            assert(weak[slot] == NULL);
            assert(ios27_objc20c_weak_unregister(rt, &weak[slot]) == 0);
            entries[slot] = NULL;
        }
        if ((iter & 127u) == 0) assert(ios27_objc20c_validate(rt) == 0);
    }
    for (size_t i = 0; i < 128; ++i) {
        if (!entries[i]) continue;
        assert(ios27_objc20c_release(rt, entries[i]) == 0);
        assert(ios27_objc20c_weak_unregister(rt, &weak[i]) == 0);
    }
    assert(ios27_objc20c_validate(rt) == 0);
    ios27_objc20c_destroy(rt);
}

typedef struct { void *next; unsigned called; } requeue_ctx;
static void queue_during_destruction(ios27_objc20c_runtime *rt,
                                     void *object, void *arg) {
    requeue_ctx *ctx = arg;
    (void)object;
    ++ctx->called;
    assert(ios27_objc20c_autorelease(rt, ctx->next) == 0);
}
static void parent_pool_requeue(void) {
    ios27_objc20c_runtime *rt = NULL;
    uint64_t outer, inner;
    void *first = NULL, *next = NULL;
    requeue_ctx ctx = {0};
    ios27_objc20c_stats stats;
    assert(ios27_objc20c_create(&rt) == 0);
    assert(ios27_objc20c_pool_push(rt, &outer) == 0);
    assert(ios27_objc20c_pool_push(rt, &inner) == 0);
    assert(ios27_objc20c_alloc(rt, sizeof(node), NULL, NULL, &next) == 0);
    ctx.next = next;
    assert(ios27_objc20c_alloc(rt, sizeof(node), queue_during_destruction,
                              &ctx, &first) == 0);
    assert(ios27_objc20c_autorelease(rt, first) == 0);
    assert(ios27_objc20c_pool_pop(rt, inner) == 0);
    ios27_objc20c_get_stats(rt, &stats);
    assert(ctx.called == 1 && stats.live_objects == 1);
    assert(stats.pending_autoreleases == 1 && stats.pool_depth == 1);
    assert(ios27_objc20c_pool_pop(rt, outer) == 0);
    ios27_objc20c_get_stats(rt, &stats);
    assert(stats.live_objects == 0 && stats.pending_autoreleases == 0);
    assert(ios27_objc20c_validate(rt) == 0);
    ios27_objc20c_destroy(rt);
}
static void byte_budget_and_cycle_shutdown(void) {
    ios27_objc20c_runtime *rt = NULL;
    void *objects[IOS27_OBJC20C_MAX_BYTES / IOS27_OBJC20C_MAX_OBJECT_BYTES];
    void *next = NULL;
    ios27_objc20c_stats stats;
    assert(ios27_objc20c_create(&rt) == 0);
    for (size_t i = 0; i < sizeof(objects) / sizeof(objects[0]); ++i)
        assert(ios27_objc20c_alloc(rt, IOS27_OBJC20C_MAX_OBJECT_BYTES,
                                  NULL, NULL, &objects[i]) == 0);
    assert(ios27_objc20c_alloc(rt, sizeof(node), NULL, NULL, &next) == -1);
    for (size_t i = 0; i < sizeof(objects) / sizeof(objects[0]); ++i)
        assert(ios27_objc20c_release(rt, objects[i]) == 0);
    assert(ios27_objc20c_alloc(rt, sizeof(node), NULL, NULL, &next) == -1);
    ios27_objc20c_get_stats(rt, &stats);
    assert(stats.lifetime_bytes == IOS27_OBJC20C_MAX_BYTES);
    assert(!stats.live_bytes && !stats.live_objects);
    ios27_objc20c_destroy(rt);
    /* A cycle is not garbage-collected by retain/release. Runtime shutdown
     * reclaims its payloads without calling destructors on dangling peers. */
    assert(ios27_objc20c_create(&rt) == 0);
    node *a = NULL, *b = NULL;
    assert(ios27_objc20c_alloc(rt, sizeof(node), NULL, NULL, (void **)&a) == 0);
    assert(ios27_objc20c_alloc(rt, sizeof(node), NULL, NULL, (void **)&b) == 0);
    assert(ios27_objc20c_strong_store(rt, &a->child, b) == 0);
    assert(ios27_objc20c_strong_store(rt, &b->child, a) == 0);
    assert(ios27_objc20c_release(rt, a) == 0);
    assert(ios27_objc20c_release(rt, b) == 0);
    ios27_objc20c_get_stats(rt, &stats);
    assert(stats.live_objects == 2);
    ios27_objc20c_destroy(rt);
}

int main(void) {
    basic_and_graph();
    pools();
    limits();
    overflow_and_drain();
    randomized();
    parent_pool_requeue();
    byte_budget_and_cycle_shutdown();
    puts("phase20c: object ownership, side tables, pools, quarantine, stress ok");
    return 0;
}
