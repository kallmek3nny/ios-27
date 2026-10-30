#define _POSIX_C_SOURCE 200809L
#include "ios27/phase25c_lifetime.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    void *isa;
    void *strong;
    void *weak;
    uint64_t marker;
} node;
static int class_token;
static const ios27_phase25c_class klass = {
    &class_token, 17, sizeof(node), _Alignof(node),
    offsetof(node, strong), 2 * sizeof(void *)
};
static atomic_uint destroyed;
static atomic_uint failures;
static unsigned order[8], order_count;

static void dealloc(ios27_phase25c_runtime *rt, void *p, void *context) {
    node *n = p;
    if (n->strong) assert(ios27_phase25c_store_strong(rt, &n->strong, NULL) == 0);
    if (context) {
        unsigned id = *(unsigned *)context;
        assert(order_count < 8);
        order[order_count++] = id;
    }
    atomic_fetch_add(&destroyed, 1);
}
static node *make(ios27_phase25c_runtime *rt, void *context) {
    void *p = NULL;
    assert(ios27_phase25c_alloc(rt, &klass, dealloc, context, &p) == 0);
    assert(ios27_phase25c_identity(rt, p, klass.isa, klass.image_generation) == 0);
    return p;
}
static void arc_entrypoints(ios27_phase25c_runtime *rt) {
    node *a = make(rt, NULL);
    void *strong = NULL, *weak = NULL;
    ios27_phase25c_runtime *prev = ios27_phase25c_scope_enter(rt);
    assert(objc_retain(a) == a);
    objc_release(a);
    objc_storeStrong(&strong, a);
    objc_release(a);
    assert(objc_storeWeak(&weak, strong) == strong);
    void *held = objc_loadWeakRetained(&weak);
    assert(held == strong);
    objc_release(held);
    objc_storeStrong(&strong, NULL);
    assert(weak == NULL && strong == NULL);
    assert(objc_loadWeakRetained(&weak) == NULL);
    objc_destroyWeak(&weak);
    void *outer = objc_autoreleasePoolPush();
    unsigned one = 1, two = 2;
    node *x = make(rt, &one);
    assert(objc_autorelease(x) == x);
    void *inner = objc_autoreleasePoolPush();
    node *y = make(rt, &two);
    assert(objc_autorelease(y) == y);
    assert(ios27_phase25c_pool_pop(rt, outer) == -1);
    objc_autoreleasePoolPop(inner);
    assert(order_count == 1 && order[0] == 2);
    objc_autoreleasePoolPop(outer);
    assert(order_count == 2 && order[1] == 1);
    ios27_phase25c_scope_leave(prev);
    assert(ios27_phase25c_thread_detach(rt) == 0);
}
static void retain_stress(ios27_phase25c_runtime *rt) {
    node *a = make(rt, NULL);
    for (unsigned i = 0; i < 120000; ++i) {
        assert(ios27_phase25c_retain(rt, a) == 0);
        assert(ios27_phase25c_release(rt, a) == 0);
    }
    uint32_t count = 0;
    assert(ios27_phase25c_retain_count(rt, a, &count) == 0 && count == 1);
    assert(ios27_phase25c_can_unload(rt, klass.isa, 17) == -1);
    assert(ios27_phase25c_release(rt, a) == 0);
    assert(ios27_phase25c_retain(rt, a) == -1);
    assert(ios27_phase25c_identity(rt, a, klass.isa, 17) == -1);
    assert(ios27_phase25c_can_unload(rt, klass.isa, 17) == 0);
}
/* Portable generation barrier for Linux and macOS. */
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    unsigned arrived, generation, total;
} phase25c_barrier;
static void gate_init(phase25c_barrier *gate, unsigned total) {
    memset(gate, 0, sizeof(*gate));
    assert(pthread_mutex_init(&gate->mutex, NULL) == 0);
    assert(pthread_cond_init(&gate->cond, NULL) == 0);
    gate->total = total;
}
static void gate_wait(phase25c_barrier *gate) {
    assert(pthread_mutex_lock(&gate->mutex) == 0);
    unsigned generation = gate->generation;
    if (++gate->arrived == gate->total) {
        gate->arrived = 0;
        ++gate->generation;
        assert(pthread_cond_broadcast(&gate->cond) == 0);
    } else {
        while (generation == gate->generation)
            assert(pthread_cond_wait(&gate->cond, &gate->mutex) == 0);
    }
    assert(pthread_mutex_unlock(&gate->mutex) == 0);
}
static void gate_destroy(phase25c_barrier *gate) {
    assert(pthread_cond_destroy(&gate->cond) == 0);
    assert(pthread_mutex_destroy(&gate->mutex) == 0);
}
typedef struct {
    ios27_phase25c_runtime *rt;
    void **weak;
    phase25c_barrier *barrier;
    unsigned iterations;
} weak_task;
static void *weak_runner(void *arg) {
    weak_task *t = arg;
    gate_wait(t->barrier);
    for (unsigned i = 0; i < t->iterations; ++i) {
        void *p = NULL;
        int rc = ios27_phase25c_load_weak_retained(t->rt, t->weak, &p);
        if (rc) { atomic_fetch_add(&failures, 1); continue; }
        if (p && ios27_phase25c_release(t->rt, p))
            atomic_fetch_add(&failures, 1);
    }
    return NULL;
}
static void weak_race(ios27_phase25c_runtime *rt) {
    node *a = make(rt, NULL);
    void *weak = NULL;
    assert(ios27_phase25c_store_weak(rt, &weak, a) == 0);
    phase25c_barrier barrier;
    gate_init(&barrier, 5);
    pthread_t threads[4];
    weak_task tasks[4];
    for (size_t i = 0; i < 4; ++i) {
        tasks[i] = (weak_task){rt, &weak, &barrier, 35000};
        assert(pthread_create(&threads[i], NULL, weak_runner, &tasks[i]) == 0);
    }
    gate_wait(&barrier);
    assert(ios27_phase25c_release(rt, a) == 0);
    for (size_t i = 0; i < 4; ++i) assert(pthread_join(threads[i], NULL) == 0);
    assert(weak == NULL && atomic_load(&failures) == 0);
    gate_destroy(&barrier);
    assert(ios27_phase25c_destroy_weak(rt, &weak) == 0);
}
static void dispatch_terminal(ios27_phase25c_runtime *rt) {
    node *a = make(rt, NULL);
    void *weak = NULL;
    assert(ios27_phase25c_store_weak(rt, &weak, a) == 0);
    assert(ios27_phase25c_pin(rt, a) == 0);
    unsigned before = atomic_load(&destroyed);
    assert(ios27_phase25c_release(rt, a) == 0);
    assert(weak == NULL);
    assert(ios27_phase25c_retain(rt, a) == -1);
    assert(atomic_load(&destroyed) == before);
    assert(ios27_phase25c_can_unload(rt, klass.isa, 17) == -1);
    assert(ios27_phase25c_unpin(rt, a) == 0);
    assert(atomic_load(&destroyed) == before + 1);
    assert(ios27_phase25c_destroy_weak(rt, &weak) == 0);
}
typedef struct { ios27_phase25c_runtime *rt; phase25c_barrier *barrier; } pool_task;
static void *pool_worker(void *arg) {
    pool_task *t = arg;
    void *pool = NULL;
    assert(ios27_phase25c_pool_push(t->rt, &pool) == 0);
    for (unsigned i = 0; i < 25; ++i) {
        node *a = make(t->rt, NULL);
        assert(ios27_phase25c_autorelease(t->rt, a) == 0);
    }
    gate_wait(t->barrier);
    assert(ios27_phase25c_pool_pop(t->rt, pool) == 0);
    /* Deliberately omit detach to verify pthread TLS destructor drains. */
    return NULL;
}
static void concurrent_pool_drains(ios27_phase25c_runtime *rt) {
    phase25c_barrier barrier;
    gate_init(&barrier, 5);
    pthread_t threads[4];
    pool_task t = { rt, &barrier };
    for (size_t i = 0; i < 4; ++i)
        assert(pthread_create(&threads[i], NULL, pool_worker, &t) == 0);
    gate_wait(&barrier);
    for (size_t i = 0; i < 4; ++i) assert(pthread_join(threads[i], NULL) == 0);
    gate_destroy(&barrier);
}
static void allocation_rollback(ios27_phase25c_runtime *rt) {
    ios27_phase25c_stats before, after;
    ios27_phase25c_get_stats(rt, &before);
    ios27_phase25c_fail_alloc_after(rt, 0);
    void *p = (void *)1;
    assert(ios27_phase25c_alloc(rt, &klass, dealloc, NULL, &p) == -1);
    assert(p == NULL);
    ios27_phase25c_get_stats(rt, &after);
    assert(after.allocations == before.allocations);
    assert(after.live_objects == before.live_objects);
    ios27_phase25c_fail_alloc_after(rt, -1);
    ios27_phase25c_class invalid = klass;
    invalid.ivar_offset = invalid.instance_size;
    invalid.ivar_bytes = 1;
    assert(ios27_phase25c_alloc(rt, &invalid, dealloc, NULL, &p) == -1);
    assert(p == NULL);
    node *a = make(rt, NULL);
    assert(ios27_phase25c_release(rt, a) == 0);
}
static void ownership_graph(ios27_phase25c_runtime *rt) {
    node *parent = make(rt, NULL);
    node *child = make(rt, NULL);
    assert(ios27_phase25c_store_strong(rt, &parent->strong, child) == 0);
    assert(ios27_phase25c_release(rt, child) == 0);
    assert(ios27_phase25c_release(rt, parent) == 0);
}
int main(void) {
    atomic_init(&destroyed, 0);
    atomic_init(&failures, 0);
    ios27_phase25c_runtime *rt = NULL;
    assert(ios27_phase25c_create(&rt) == 0);
    arc_entrypoints(rt);
    retain_stress(rt);
    weak_race(rt);
    dispatch_terminal(rt);
    concurrent_pool_drains(rt);
    ownership_graph(rt);
    allocation_rollback(rt);
    ios27_phase25c_stats stats;
    ios27_phase25c_get_stats(rt, &stats);
    assert(stats.transitions >= 240000);
    assert(stats.weak_loads >= 140000);
    assert(stats.live_objects == 0 && stats.weak_slots == 0);
    assert(stats.thread_contexts == 0);
    assert(stats.allocations == stats.deallocations);
    assert(ios27_phase25c_validate(rt) == 0);
    printf("phase25c: %llu transitions, %llu concurrent weak loads, %llu zeroed, %llu finalized\n",
        (unsigned long long)stats.transitions, (unsigned long long)stats.weak_loads,
        (unsigned long long)stats.weak_zeroed, (unsigned long long)stats.deallocations);
    assert(ios27_phase25c_destroy(&rt) == 0 && rt == NULL);
    for (unsigned i = 0; i < 24; ++i) {
        assert(ios27_phase25c_create(&rt) == 0);
        node *a = make(rt, NULL);
        assert(ios27_phase25c_destroy(&rt) == -1 && rt != NULL);
        assert(ios27_phase25c_release(rt, a) == 0);
        assert(ios27_phase25c_destroy(&rt) == 0 && rt == NULL);
    }
    puts("phase25c: host-native ownership, ARC entrypoints, concurrent weak, TLS pools, teardown ok");
    return 0;
}
