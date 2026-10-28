#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21b_lifecycle.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    int open;
} gate;
static void init_gate(gate *g) {
    assert(!pthread_mutex_init(&g->mutex, NULL));
    assert(!pthread_cond_init(&g->changed, NULL));
    g->open = 0;
}
static void release_gate(gate *g) {
    assert(!pthread_mutex_lock(&g->mutex));
    g->open = 1;
    assert(!pthread_cond_broadcast(&g->changed));
    assert(!pthread_mutex_unlock(&g->mutex));
}
static void await_gate(gate *g) {
    assert(!pthread_mutex_lock(&g->mutex));
    while (!g->open) assert(!pthread_cond_wait(&g->changed, &g->mutex));
    assert(!pthread_mutex_unlock(&g->mutex));
}
static void destroy_gate(gate *g) {
    assert(!pthread_cond_destroy(&g->changed));
    assert(!pthread_mutex_destroy(&g->mutex));
}
typedef struct {
    ios27_21b_runtime *rt;
    gate *g;
    atomic_int *count;
    ios27_21b_thread observed;
} work;
static void *job(void *context) {
    work *w = context;
    w->observed = ios27_21b_thread_self(w->rt);
    if (w->g) await_gate(w->g);
    if (w->count) atomic_fetch_add_explicit(w->count, 1, memory_order_relaxed);
    return w;
}
static void *no_op(void *unused) { return unused; }
static void validate_attrs(void) {
    ios27_21b_attrs a;
    assert(ios27_21b_attr_init(NULL) == EINVAL);
    assert(!ios27_21b_attr_init(&a));
    assert(!ios27_21b_attr_validate(&a));
    a.version = 0;
    assert(ios27_21b_attr_validate(&a) == EINVAL);
    a.version = IOS27_21B_ATTR_VERSION;
    a.detached = 2;
    assert(ios27_21b_attr_validate(&a) == EINVAL);
    a.detached = 0;
    a.scheduling = 1;
    assert(ios27_21b_attr_validate(&a) == ENOTSUP);
    a.scheduling = 0;
    a.priority = 1;
    assert(ios27_21b_attr_validate(&a) == ENOTSUP);
    a.priority = 0;
    a.stack_address = 0x1000;
    assert(ios27_21b_attr_validate(&a) == ENOTSUP);
    a.stack_address = 0;
    a.stack_bytes = 4095;
    assert(ios27_21b_attr_validate(&a) == EINVAL);
    a.stack_bytes = 0;
    a.guard_bytes = 123;
    assert(ios27_21b_attr_validate(&a) == EINVAL);
    a.guard_bytes = 0;
    a.stack_bytes = UINT64_MAX;
    assert(ios27_21b_attr_validate(&a) == EINVAL);
    a.stack_bytes = 1024u * 1024u;
    a.guard_bytes = 4096;
    assert(!ios27_21b_attr_validate(&a));
}
static void lifecycle(void) {
    ios27_21b_runtime *rt = NULL;
    ios27_21b_thread id, stale;
    ios27_21b_info info;
    size_t n;
    gate g;
    work w;
    void *value = NULL;
    atomic_int count = 0;
    assert(ios27_21b_runtime_create(NULL, 3) == EINVAL);
    assert(ios27_21b_runtime_create(&rt, 0) == EINVAL);
    assert(ios27_21b_runtime_create(&rt, 65) == EINVAL);
    assert(!ios27_21b_runtime_create(&rt, 1));
    init_gate(&g);
    w = (work){rt, &g, &count, {0}};
    assert(ios27_21b_thread_create(rt, NULL, NULL, job, &w) == EINVAL);
    assert(ios27_21b_thread_create(rt, &id, NULL, NULL, &w) == EINVAL);
    assert(!ios27_21b_thread_create(rt, &id, NULL, job, &w));
    stale = id;
    assert(ios27_21b_runtime_destroy(rt) == EBUSY);
    assert(!ios27_21b_runtime_usage(rt, &n) && n == 1);
    assert(ios27_21b_runtime_wait_idle(rt, 2) == ETIMEDOUT);
    assert(ios27_21b_thread_create(rt, &stale, NULL, no_op, NULL) == EAGAIN);
    assert(!stale.slot && !stale.generation);
    assert(!ios27_21b_thread_info(rt, id, &info) && !info.detached);
    release_gate(&g);
    assert(!ios27_21b_thread_join(rt, id, &value) && value == &w);
    assert(ios27_21b_thread_equal(w.observed, id));
    assert(!ios27_21b_thread_equal(w.observed, (ios27_21b_thread){0}));
    assert(ios27_21b_thread_join(rt, id, NULL) == ESRCH);
    assert(ios27_21b_thread_detach(rt, id) == ESRCH);
    assert(!ios27_21b_runtime_usage(rt, &n) && n == 0);
    assert(!ios27_21b_runtime_wait_idle(rt, 20));
    assert(!ios27_21b_thread_create(rt, &stale, NULL, no_op, NULL));
    assert(stale.slot == id.slot && stale.generation != id.generation);
    assert(ios27_21b_thread_join(rt, id, NULL) == ESRCH);
    assert(!ios27_21b_thread_join(rt, stale, NULL));
    destroy_gate(&g);
    assert(!ios27_21b_runtime_destroy(rt));
    assert(atomic_load(&count) == 1);
}

typedef struct {
    ios27_21b_runtime *rt;
    ios27_21b_thread id;
    pthread_barrier_t *barrier;
    int detach;
    int error;
} contender;
static void *contend(void *v) {
    contender *a = v;
    int b = pthread_barrier_wait(a->barrier);
    assert(!b || b == PTHREAD_BARRIER_SERIAL_THREAD);
    a->error = a->detach ? ios27_21b_thread_detach(a->rt, a->id) :
                           ios27_21b_thread_join(a->rt, a->id, NULL);
    return NULL;
}
static void race_join_detach(void) {
    ios27_21b_runtime *rt = NULL;
    int i;
    assert(!ios27_21b_runtime_create(&rt, 2));
    for (i = 0; i < 128; ++i) {
        gate g;
        work w;
        ios27_21b_thread id;
        pthread_t first, second;
        pthread_barrier_t barrier;
        contender a, b;
        init_gate(&g);
        w = (work){rt, &g, NULL, {0}};
        assert(!ios27_21b_thread_create(rt, &id, NULL, job, &w));
        assert(!pthread_barrier_init(&barrier, NULL, 3));
        a = (contender){rt, id, &barrier, 0, -1};
        b = (contender){rt, id, &barrier, 1, -1};
        assert(!pthread_create(&first, NULL, contend, &a));
        assert(!pthread_create(&second, NULL, contend, &b));
        int v = pthread_barrier_wait(&barrier);
        assert(!v || v == PTHREAD_BARRIER_SERIAL_THREAD);
        release_gate(&g);
        assert(!pthread_join(first, NULL));
        assert(!pthread_join(second, NULL));
        assert((!a.error) != (!b.error));
        assert((a.error == 0 || a.error == EINVAL || a.error == ESRCH) &&
               (b.error == 0 || b.error == EINVAL || b.error == ESRCH));
        assert(!ios27_21b_runtime_wait_idle(rt, 3000));
        assert(ios27_21b_thread_join(rt, id, NULL) == ESRCH);
        assert(!pthread_barrier_destroy(&barrier));
        destroy_gate(&g);
    }
    assert(!ios27_21b_runtime_destroy(rt));
}
static atomic_int tls_cleaned = 0;
static pthread_key_t tls_key;
static void tls_dtor(void *p) { if (p) atomic_fetch_add(&tls_cleaned, 1); }
static void *tls_job(void *unused) {
    assert(!pthread_setspecific(tls_key, (void *)&tls_key));
    return unused;
}
static void detached_and_stress(void) {
    ios27_21b_runtime *rt = NULL;
    ios27_21b_attrs attr;
    ios27_21b_thread ids[8];
    gate g;
    work w[8];
    atomic_int count = 0;
    size_t n;
    int i;
    assert(!ios27_21b_runtime_create(&rt, 8));
    assert(!ios27_21b_attr_init(&attr));
    attr.detached = 1;
    init_gate(&g);
    for (i = 0; i < 8; ++i) {
        w[i] = (work){rt, &g, &count, {0}};
        assert(!ios27_21b_thread_create(rt, &ids[i], &attr, job, &w[i]));
    }
    assert(ios27_21b_thread_create(rt, &ids[0], &attr, job, &w[0]) == EAGAIN);
    assert(!ios27_21b_runtime_usage(rt, &n) && n == 8);
    assert(ios27_21b_runtime_wait_idle(rt, 1) == ETIMEDOUT);
    release_gate(&g);
    assert(!ios27_21b_runtime_wait_idle(rt, 3000));
    assert(atomic_load(&count) == 8);
    assert(!ios27_21b_runtime_usage(rt, &n) && n == 0);
    destroy_gate(&g);
    assert(!pthread_key_create(&tls_key, tls_dtor));
    for (i = 0; i < 2048; ++i) {
        ios27_21b_thread id;
        if (i % 2) {
            assert(!ios27_21b_thread_create(rt, &id, &attr, tls_job, NULL));
            assert(!ios27_21b_runtime_wait_idle(rt, 3000));
        } else {
            assert(!ios27_21b_thread_create(rt, &id, NULL, tls_job, NULL));
            assert(!ios27_21b_thread_join(rt, id, NULL));
        }
        assert(ios27_21b_thread_join(rt, id, NULL) == ESRCH);
    }
    assert(atomic_load(&tls_cleaned) == 2048);
    assert(!pthread_key_delete(tls_key));
    assert(!ios27_21b_runtime_destroy(rt));
}
static void explicit_stack_and_self(void) {
    ios27_21b_runtime *rt = NULL;
    ios27_21b_attrs a;
    ios27_21b_thread id;
    assert(!ios27_21b_runtime_create(&rt, 1));
    assert(!ios27_21b_attr_init(&a));
    a.stack_bytes = 1024u * 1024u;
    a.guard_bytes = 4096;
    assert(!ios27_21b_thread_create(rt, &id, &a, no_op, NULL));
    assert(!ios27_21b_thread_join(rt, id, NULL));
    assert(!ios27_21b_thread_self(rt).slot);
    assert(!ios27_21b_runtime_destroy(rt));
}

typedef struct { ios27_21b_runtime *rt; int observed; gate *ack; } self_join_context;
static void *self_join_job(void *p) {
    self_join_context *ctx = p;
    ctx->observed = ios27_21b_thread_join(ctx->rt,
        ios27_21b_thread_self(ctx->rt), NULL);
    release_gate(ctx->ack);
    return ctx;
}
static void identity_isolation(void) {
    ios27_21b_runtime *left = NULL, *right = NULL;
    ios27_21b_thread a, b;
    self_join_context ctx;
    gate ack;
    init_gate(&ack);
    assert(!ios27_21b_runtime_create(&left, 1));
    assert(!ios27_21b_runtime_create(&right, 1));
    ctx.rt = left; ctx.observed = -1; ctx.ack = &ack;
    assert(!ios27_21b_thread_create(left, &a, NULL, self_join_job, &ctx));
    assert(!ios27_21b_thread_create(right, &b, NULL, no_op, NULL));
    assert(a.slot == b.slot && a.generation == b.generation);
    assert(a.owner != b.owner);
    assert(!ios27_21b_thread_equal(a, b));
    assert(ios27_21b_thread_join(right, a, NULL) == ESRCH);
    assert(ios27_21b_thread_detach(left, b) == ESRCH);
    await_gate(&ack);
    assert(!ios27_21b_thread_join(left, a, NULL));
    assert(ctx.observed == EDEADLK);
    assert(!ios27_21b_thread_join(right, b, NULL));
    assert(!ios27_21b_runtime_destroy(left));
    assert(!ios27_21b_runtime_destroy(right));
    destroy_gate(&ack);
}
static void race_double_join(void) {
    ios27_21b_runtime *rt = NULL;
    assert(!ios27_21b_runtime_create(&rt, 1));
    for (int i = 0; i < 64; ++i) {
        gate g;
        work w;
        ios27_21b_thread id;
        pthread_t t1, t2;
        pthread_barrier_t b;
        contender a, c;
        init_gate(&g);
        w = (work){rt, &g, NULL, {0}};
        assert(!ios27_21b_thread_create(rt, &id, NULL, job, &w));
        assert(!pthread_barrier_init(&b, NULL, 3));
        a = (contender){rt, id, &b, 0, -1};
        c = (contender){rt, id, &b, 0, -1};
        assert(!pthread_create(&t1, NULL, contend, &a));
        assert(!pthread_create(&t2, NULL, contend, &c));
        int v = pthread_barrier_wait(&b);
        assert(!v || v == PTHREAD_BARRIER_SERIAL_THREAD);
        release_gate(&g);
        assert(!pthread_join(t1, NULL));
        assert(!pthread_join(t2, NULL));
        assert((!a.error) != (!c.error));
        assert((a.error == 0 || a.error == EINVAL || a.error == ESRCH) &&
               (c.error == 0 || c.error == EINVAL || c.error == ESRCH));
        assert(!ios27_21b_runtime_wait_idle(rt, 3000));
        assert(!pthread_barrier_destroy(&b));
        destroy_gate(&g);
    }
    assert(!ios27_21b_runtime_destroy(rt));
}

int main(void) {
    validate_attrs();
    identity_isolation();
    race_double_join();
    lifecycle();
    race_join_detach();
    detached_and_stress();
    explicit_stack_and_self();
    puts("phase21b: 4 lifecycle milestones, 192 ownership races and 2048 TLS-cleaned transitions passed");
    return 0;
}
