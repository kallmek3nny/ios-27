#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21d_rw_once.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(call) do { int rc_ = (call); if (rc_) { \
    fprintf(stderr, "%s:%d: %s -> %d\n", __FILE__, __LINE__, #call, rc_); abort(); \
} } while (0)
#define REQUIRE(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition); abort(); \
} } while (0)

enum { BUCKETS = 64, READERS = 32, WRITERS = 16, OPS = 1200 };
typedef struct {
    ios27_21d_runtime *rt;
    ios27_21d_handle rw, once;
    pthread_barrier_t start;
    int64_t values[BUCKETS];
    int64_t mirrors[BUCKETS];
    int64_t aggregate;
    atomic_int initialized;
    atomic_int init_calls;
    atomic_long reads, writes;
} store;
typedef struct {
    store *database;
    int id;
} agent;

static int initialize_store(void *opaque) {
    store *s = opaque;
    for (int k = 0; k < BUCKETS; ++k) {
        s->values[k] = k;
        s->mirrors[k] = 7 * (int64_t)k;
        s->aggregate += k;
    }
    atomic_fetch_add(&s->init_calls, 1);
    atomic_store_explicit(&s->initialized, 1, memory_order_release);
    return 0;
}

static uint32_t next_random(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void verify_store(const store *s) {
    int64_t aggregate = 0;
    for (int k = 0; k < BUCKETS; ++k) {
        REQUIRE(s->mirrors[k] == 7 * s->values[k]);
        aggregate += s->values[k];
    }
    REQUIRE(aggregate == s->aggregate);
}

static void *database_agent(void *opaque) {
    agent *a = opaque;
    store *s = a->database;
    uint32_t seed = UINT32_C(0x9e3779b9) ^ (uint32_t)(a->id + 1);
    int ready = pthread_barrier_wait(&s->start);
    REQUIRE(ready == 0 || ready == PTHREAD_BARRIER_SERIAL_THREAD);
    for (int n = 0; n < OPS; ++n) {
        CHECK(ios27_21d_once(s->rt, s->once, initialize_store, s));
        REQUIRE(atomic_load_explicit(&s->initialized, memory_order_acquire));
        size_t bucket = (size_t)(next_random(&seed) % BUCKETS);
        if (a->id >= READERS) {
            CHECK(ios27_21d_wrlock(s->rt, s->rw));
            ++s->values[bucket];
            s->mirrors[bucket] = 7 * s->values[bucket];
            ++s->aggregate;
            CHECK(ios27_21d_unlock(s->rt, s->rw));
            atomic_fetch_add(&s->writes, 1);
        } else {
            CHECK(ios27_21d_rdlock(s->rt, s->rw));
            verify_store(s);
            CHECK(ios27_21d_unlock(s->rt, s->rw));
            atomic_fetch_add(&s->reads, 1);
        }
    }
    return NULL;
}

int main(void) {
    store s = {0};
    pthread_t workers[READERS + WRITERS];
    agent args[READERS + WRITERS];
    CHECK(ios27_21d_runtime_create(&s.rt, 2));
    CHECK(ios27_21d_create(s.rt, IOS27_21D_RWLOCK, &s.rw));
    CHECK(ios27_21d_create(s.rt, IOS27_21D_ONCE, &s.once));
    CHECK(pthread_barrier_init(&s.start, NULL, READERS + WRITERS + 1));
    for (int i = 0; i < READERS + WRITERS; ++i) {
        args[i] = (agent){&s, i};
        CHECK(pthread_create(&workers[i], NULL, database_agent, &args[i]));
    }
    int rc = pthread_barrier_wait(&s.start);
    REQUIRE(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    for (int i = 0; i < READERS + WRITERS; ++i) CHECK(pthread_join(workers[i], NULL));
    REQUIRE(atomic_load(&s.init_calls) == 1);
    REQUIRE(atomic_load(&s.reads) == (long)READERS * OPS);
    REQUIRE(atomic_load(&s.writes) == (long)WRITERS * OPS);
    verify_store(&s);
    REQUIRE(s.aggregate == (int64_t)BUCKETS * (BUCKETS - 1) / 2 +
                           (int64_t)WRITERS * OPS);
    CHECK(pthread_barrier_destroy(&s.start));
    CHECK(ios27_21d_destroy(s.rt, s.once));
    CHECK(ios27_21d_destroy(s.rt, s.rw));
    CHECK(ios27_21d_runtime_destroy(s.rt));
    puts("phase21d integration: 48 native agents, 57,600 shared-map operations and single once init");
    return 0;
}
