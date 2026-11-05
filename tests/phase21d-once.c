#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21d_rw_once.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(call) do { int rc_ = (call); if (rc_) { \
    fprintf(stderr, "%s:%d: %s -> %d\n", __FILE__, __LINE__, #call, rc_); abort(); \
} } while (0)
#define REQUIRE(cond) do { if (!(cond)) { \
    fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #cond); abort(); \
} } while (0)
#define NTHREADS 64

typedef struct {
    ios27_21d_runtime *rt;
    ios27_21d_handle once;
    pthread_barrier_t *start;
    atomic_int attempted;
    atomic_int initialized;
    atomic_int failed;
    int published_value;
} once_data;

static int initialize(void *context) {
    once_data *s = context;
    int count = atomic_fetch_add(&s->attempted, 1);
    if (count == 0) { atomic_fetch_add(&s->failed, 1); return EAGAIN; }
    s->published_value = 0x12345678;
    atomic_fetch_add(&s->initialized, 1);
    return 0;
}

static void *runner(void *opaque) {
    once_data *s = opaque;
    int barrier = pthread_barrier_wait(s->start);
    REQUIRE(barrier == 0 || barrier == PTHREAD_BARRIER_SERIAL_THREAD);
    for (int i = 0; i < 3; ++i) {
        int err = ios27_21d_once(s->rt, s->once, initialize, s);
        if (!err) {
            REQUIRE(s->published_value == 0x12345678);
            return NULL;
        }
        REQUIRE(err == EAGAIN);
    }
    REQUIRE(0 && "once retries exhausted");
    return NULL;
}

static void test_once_race(void) {
    once_data s = {0};
    pthread_t threads[NTHREADS];
    pthread_barrier_t start;
    CHECK(ios27_21d_runtime_create(&s.rt, 4));
    CHECK(ios27_21d_create(s.rt, IOS27_21D_ONCE, &s.once));
    CHECK(pthread_barrier_init(&start, NULL, NTHREADS + 1));
    s.start = &start;
    for (int i = 0; i < NTHREADS; ++i)
        CHECK(pthread_create(&threads[i], NULL, runner, &s));
    int ready = pthread_barrier_wait(&start);
    REQUIRE(ready == 0 || ready == PTHREAD_BARRIER_SERIAL_THREAD);
    for (int i = 0; i < NTHREADS; ++i) CHECK(pthread_join(threads[i], NULL));
    CHECK(pthread_barrier_destroy(&start));
    REQUIRE(atomic_load(&s.attempted) == 2);
    REQUIRE(atomic_load(&s.failed) == 1);
    REQUIRE(atomic_load(&s.initialized) == 1);
    CHECK(ios27_21d_once(s.rt, s.once, initialize, &s));
    REQUIRE(atomic_load(&s.attempted) == 2);
    CHECK(ios27_21d_destroy(s.rt, s.once));
    REQUIRE(ios27_21d_once(s.rt, s.once, initialize, &s) == EINVAL);
    CHECK(ios27_21d_runtime_destroy(s.rt));
    puts("phase21d once: 64-way race, memory publication, one rollback and one success");
}

typedef struct {
    ios27_21d_runtime *rt;
    ios27_21d_handle handle;
    atomic_int calls;
} recursive_data;

static int recursive_init(void *ctx) {
    recursive_data *s = ctx;
    atomic_fetch_add(&s->calls, 1);
    REQUIRE(ios27_21d_once(s->rt, s->handle, recursive_init, s) == EDEADLK);
    REQUIRE(ios27_21d_destroy(s->rt, s->handle) == EBUSY);
    return 0;
}
static int always_fail(void *ctx) {
    int *attempts = ctx;
    ++*attempts;
    return ENOMEM;
}

static int negative_fail(void *ctx) {
    int *attempts = ctx;
    ++*attempts;
    return -EAGAIN;
}
static void test_reentrancy_and_retry(void) {
    recursive_data s = {0};
    ios27_21d_handle failed;
    int attempts = 0;
    CHECK(ios27_21d_runtime_create(&s.rt, 2));
    CHECK(ios27_21d_create(s.rt, IOS27_21D_ONCE, &s.handle));
    CHECK(ios27_21d_create(s.rt, IOS27_21D_ONCE, &failed));
    REQUIRE(ios27_21d_once(s.rt, s.handle, NULL, &s) == EINVAL);
    CHECK(ios27_21d_once(s.rt, s.handle, recursive_init, &s));
    CHECK(ios27_21d_once(s.rt, s.handle, recursive_init, &s));
    REQUIRE(atomic_load(&s.calls) == 1);
    REQUIRE(ios27_21d_once(s.rt, failed, negative_fail, &attempts) == EINVAL);
    for (int i = 0; i < 1500; ++i)
        REQUIRE(ios27_21d_once(s.rt, failed, always_fail, &attempts) == ENOMEM);
    REQUIRE(attempts == 1501);
    CHECK(ios27_21d_destroy(s.rt, failed));
    CHECK(ios27_21d_destroy(s.rt, s.handle));
    CHECK(ios27_21d_runtime_destroy(s.rt));
    puts("phase21d once: recursive EDEADLK, busy destruction and repeated rollback");
}

static void test_nested_and_cross_thread_cycles(void);
static void test_three_way_cycles(void);
static void test_ring_cycles(void);

int main(void) {
    test_ring_cycles();
    test_three_way_cycles();
    test_nested_and_cross_thread_cycles();
    test_once_race();
    test_reentrancy_and_retry();
    return 0;
}

typedef struct {
    ios27_21d_runtime *rt;
    ios27_21d_handle a, b;
    pthread_barrier_t barrier;
    atomic_int deadlocks;
    int initialized_b;
    int initialized_a;
} graph_data;

static int inner_init(void *opaque) {
    graph_data *g = opaque;
    g->initialized_b = 27;
    return 0;
}
static int outer_init(void *opaque) {
    graph_data *g = opaque;
    CHECK(ios27_21d_once(g->rt, g->b, inner_init, g));
    REQUIRE(g->initialized_b == 27);
    g->initialized_a = 28;
    return 0;
}

static int cross_a(void *opaque);
static int cross_b(void *opaque);
static int cross_init(graph_data *g, ios27_21d_handle other,
                      ios27_21d_initializer callback) {
    int ready = pthread_barrier_wait(&g->barrier);
    REQUIRE(ready == 0 || ready == PTHREAD_BARRIER_SERIAL_THREAD);
    int err = ios27_21d_once(g->rt, other, callback, g);
    if (err == EDEADLK) atomic_fetch_add(&g->deadlocks, 1);
    else REQUIRE(err == 0);
    return 0;
}
static int cross_a(void *opaque) {
    graph_data *g = opaque;
    return cross_init(g, g->b, cross_b);
}
static int cross_b(void *opaque) {
    graph_data *g = opaque;
    return cross_init(g, g->a, cross_a);
}
static void *cross_worker_a(void *opaque) {
    graph_data *g = opaque;
    CHECK(ios27_21d_once(g->rt, g->a, cross_a, g));
    return NULL;
}
static void *cross_worker_b(void *opaque) {
    graph_data *g = opaque;
    CHECK(ios27_21d_once(g->rt, g->b, cross_b, g));
    return NULL;
}

static void test_nested_and_cross_thread_cycles(void) {
    graph_data g = {0};
    pthread_t a, b;
    CHECK(ios27_21d_runtime_create(&g.rt, 3));
    CHECK(ios27_21d_create(g.rt, IOS27_21D_ONCE, &g.a));
    CHECK(ios27_21d_create(g.rt, IOS27_21D_ONCE, &g.b));
    CHECK(ios27_21d_once(g.rt, g.a, outer_init, &g));
    REQUIRE(g.initialized_a == 28 && g.initialized_b == 27);
    CHECK(ios27_21d_destroy(g.rt, g.a));
    CHECK(ios27_21d_destroy(g.rt, g.b));
    CHECK(ios27_21d_create(g.rt, IOS27_21D_ONCE, &g.a));
    CHECK(ios27_21d_create(g.rt, IOS27_21D_ONCE, &g.b));
    CHECK(pthread_barrier_init(&g.barrier, NULL, 2));
    CHECK(pthread_create(&a, NULL, cross_worker_a, &g));
    CHECK(pthread_create(&b, NULL, cross_worker_b, &g));
    CHECK(pthread_join(a, NULL));
    CHECK(pthread_join(b, NULL));
    REQUIRE(atomic_load(&g.deadlocks) == 1);
    CHECK(pthread_barrier_destroy(&g.barrier));
    CHECK(ios27_21d_destroy(g.rt, g.a));
    CHECK(ios27_21d_destroy(g.rt, g.b));
    CHECK(ios27_21d_runtime_destroy(g.rt));
    puts("phase21d once: nested ordering and cross-thread dependency cycle rejected");
}

typedef struct {
    ios27_21d_runtime *rt;
    ios27_21d_handle handle[3];
    pthread_barrier_t barrier;
    atomic_int deadlocks;
} triple_data;
typedef struct { triple_data *data; int index; } triple_arg;
static int triple_a(void *opaque);
static int triple_b(void *opaque);
static int triple_c(void *opaque);
static int triple_step(triple_data *s, int index) {
    ios27_21d_initializer init[] = {triple_a, triple_b, triple_c};
    int rc = pthread_barrier_wait(&s->barrier);
    REQUIRE(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    rc = ios27_21d_once(s->rt, s->handle[(index + 1) % 3],
                         init[(index + 1) % 3], s);
    if (rc == EDEADLK) atomic_fetch_add(&s->deadlocks, 1);
    else REQUIRE(rc == 0);
    return 0;
}
static int triple_a(void *opaque) { return triple_step(opaque, 0); }
static int triple_b(void *opaque) { return triple_step(opaque, 1); }
static int triple_c(void *opaque) { return triple_step(opaque, 2); }
static void *triple_runner(void *opaque) {
    triple_arg *arg = opaque;
    ios27_21d_initializer init[] = {triple_a, triple_b, triple_c};
    CHECK(ios27_21d_once(arg->data->rt, arg->data->handle[arg->index],
                         init[arg->index], arg->data));
    return NULL;
}
static void test_three_way_cycles(void) {
    triple_data s = {0};
    CHECK(ios27_21d_runtime_create(&s.rt, 3));
    for (int round = 0; round < 60; ++round) {
        pthread_t worker[3];
        triple_arg args[3];
        atomic_store(&s.deadlocks, 0);
        CHECK(pthread_barrier_init(&s.barrier, NULL, 3));
        for (int i = 0; i < 3; ++i) {
            CHECK(ios27_21d_create(s.rt, IOS27_21D_ONCE, &s.handle[i]));
            args[i] = (triple_arg){&s, i};
        }
        for (int i = 0; i < 3; ++i)
            CHECK(pthread_create(&worker[i], NULL, triple_runner, &args[i]));
        for (int i = 0; i < 3; ++i) CHECK(pthread_join(worker[i], NULL));
        REQUIRE(atomic_load(&s.deadlocks) == 1);
        CHECK(pthread_barrier_destroy(&s.barrier));
        for (int i = 0; i < 3; ++i) CHECK(ios27_21d_destroy(s.rt, s.handle[i]));
    }
    CHECK(ios27_21d_runtime_destroy(s.rt));
    puts("phase21d once: 60 three-thread dependency cycles broken without hangs");
}

/* Verify that the wait-dependency traversal handles a full-capacity cycle,
   not merely a special two- or three-thread pattern. */
enum { RING = 32, RING_ROUNDS = 30 };
typedef struct {
    ios27_21d_runtime *rt;
    ios27_21d_handle handles[RING];
    pthread_barrier_t ready;
    atomic_int deadlocks;
} ring_data;
typedef struct {
    ring_data *data;
    int index;
} ring_arg;
static int ring_initialize(void *opaque) {
    ring_arg *arg = opaque;
    ring_data *s = arg->data;
    int rc = pthread_barrier_wait(&s->ready);
    REQUIRE(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    int next = (arg->index + 1) % RING;
    ring_arg next_arg = {s, next};
    rc = ios27_21d_once(s->rt, s->handles[next], ring_initialize, &next_arg);
    if (rc == EDEADLK) atomic_fetch_add(&s->deadlocks, 1);
    else REQUIRE(rc == 0);
    return 0;
}
static void *ring_runner(void *opaque) {
    ring_arg *arg = opaque;
    CHECK(ios27_21d_once(arg->data->rt, arg->data->handles[arg->index],
                         ring_initialize, arg));
    return NULL;
}
static void test_ring_cycles(void) {
    ring_data s = {0};
    CHECK(ios27_21d_runtime_create(&s.rt, RING));
    for (int round = 0; round < RING_ROUNDS; ++round) {
        ring_arg args[RING];
        pthread_t workers[RING];
        atomic_store(&s.deadlocks, 0);
        CHECK(pthread_barrier_init(&s.ready, NULL, RING));
        for (int i = 0; i < RING; ++i) {
            CHECK(ios27_21d_create(s.rt, IOS27_21D_ONCE, &s.handles[i]));
            args[i] = (ring_arg){&s, i};
        }
        for (int i = 0; i < RING; ++i)
            CHECK(pthread_create(&workers[i], NULL, ring_runner, &args[i]));
        for (int i = 0; i < RING; ++i) CHECK(pthread_join(workers[i], NULL));
        REQUIRE(atomic_load(&s.deadlocks) == 1);
        CHECK(pthread_barrier_destroy(&s.ready));
        for (int i = 0; i < RING; ++i)
            CHECK(ios27_21d_destroy(s.rt, s.handles[i]));
    }
    CHECK(ios27_21d_runtime_destroy(s.rt));
    puts("phase21d once: 30 32-thread dependency rings detected without deadlock");
}
