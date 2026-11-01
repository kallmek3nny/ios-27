#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21d_rw_once.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { int value_ = (x); if (value_) { \
  fprintf(stderr, "%s:%d: %s -> %d\n", __FILE__, __LINE__, #x, value_); abort(); \
} } while (0)
#define REQUIRE(x) do { if (!(x)) { \
  fprintf(stderr, "%s:%d: assertion %s\n", __FILE__, __LINE__, #x); abort(); \
} } while (0)

enum { WRITERS = 16, FRONT_READERS = 3, BACK_READERS = 3 };

typedef struct {
    ios27_21d_runtime *rt;
    ios27_21d_handle lock;
    atomic_int *next;
    int *order;
    int id;
    int write;
    int result;
} worker_arg;

static void *perform(void *opaque) {
    worker_arg *arg = opaque;
    int result = arg->write ? ios27_21d_wrlock(arg->rt, arg->lock) :
                              ios27_21d_rdlock(arg->rt, arg->lock);
    arg->result = result;
    if (!result) {
        int index = atomic_fetch_add(arg->next, 1);
        arg->order[index] = arg->id;
        CHECK(ios27_21d_unlock(arg->rt, arg->lock));
    }
    return NULL;
}

static void wait_for_queue(ios27_21d_runtime *rt, size_t count) {
    size_t waiting = 0;
    for (int attempt = 0; attempt < 1000000; ++attempt) {
        CHECK(ios27_21d_usage(rt, NULL, &waiting, NULL));
        if (waiting >= count) return;
        (void)sched_yield();
    }
    fprintf(stderr, "waiting for %zu queue entries, observed %zu\n", count, waiting);
    abort();
}

static void validate_empty(ios27_21d_runtime *rt, ios27_21d_handle lock) {
    size_t count = 0, waiting = 0, holders = 0;
    CHECK(ios27_21d_usage(rt, &count, &waiting, &holders));
    REQUIRE(count == 1u && waiting == 0u && holders == 0u);
    CHECK(ios27_21d_destroy(rt, lock));
    CHECK(ios27_21d_runtime_destroy(rt));
}

static void ordered_writers(void) {
    ios27_21d_runtime *rt;
    ios27_21d_handle lock;
    worker_arg args[WRITERS];
    pthread_t threads[WRITERS];
    atomic_int next = 0;
    int order[WRITERS];
    CHECK(ios27_21d_runtime_create(&rt, 1));
    CHECK(ios27_21d_create(rt, IOS27_21D_RWLOCK, &lock));
    CHECK(ios27_21d_rdlock(rt, lock));
    for (int i = 0; i < WRITERS; ++i) {
        args[i] = (worker_arg){rt, lock, &next, order, i, 1, -1};
        CHECK(pthread_create(&threads[i], NULL, perform, &args[i]));
        wait_for_queue(rt, (size_t)i + 1u);
    }
    CHECK(ios27_21d_unlock(rt, lock));
    for (int i = 0; i < WRITERS; ++i) {
        CHECK(pthread_join(threads[i], NULL));
        REQUIRE(args[i].result == 0 && order[i] == i);
    }
    REQUIRE(atomic_load(&next) == WRITERS);
    validate_empty(rt, lock);
}

static void reader_cohort_ordering(void) {
    ios27_21d_runtime *rt;
    ios27_21d_handle lock;
    enum { N = FRONT_READERS + BACK_READERS + 1 };
    worker_arg args[N];
    pthread_t threads[N];
    atomic_int next = 0;
    int order[N];
    CHECK(ios27_21d_runtime_create(&rt, 1));
    CHECK(ios27_21d_create(rt, IOS27_21D_RWLOCK, &lock));
    CHECK(ios27_21d_wrlock(rt, lock));
    for (int i = 0; i < N; ++i) {
        args[i] = (worker_arg){rt, lock, &next, order, i,
                               i == FRONT_READERS, -1};
        CHECK(pthread_create(&threads[i], NULL, perform, &args[i]));
        wait_for_queue(rt, (size_t)i + 1u);
    }
    CHECK(ios27_21d_unlock(rt, lock));
    for (int i = 0; i < N; ++i) {
        CHECK(pthread_join(threads[i], NULL));
        REQUIRE(args[i].result == 0);
    }
    REQUIRE(atomic_load(&next) == N);
    /* All first-group reader owners must exit before the queued writer can
       enter, and no second-group reader may bypass that writer. */
    for (int i = 0; i < FRONT_READERS; ++i)
        REQUIRE(order[i] >= 0 && order[i] < FRONT_READERS);
    REQUIRE(order[FRONT_READERS] == FRONT_READERS);
    for (int i = FRONT_READERS + 1; i < N; ++i)
        REQUIRE(order[i] > FRONT_READERS);
    validate_empty(rt, lock);
}

int main(void) {
    for (int round = 0; round < 25; ++round) {
        ordered_writers();
        reader_cohort_ordering();
    }
    puts("phase21d fairness: 25 fifo writer queues and reader cohorts passed");
    return 0;
}
