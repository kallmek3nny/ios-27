#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21i_abi.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define ASSERT(x) do { if (!(x)) { \
    fprintf(stderr, "phase21i recycle failure %s:%d: %s\\n", \
            __FILE__, __LINE__, #x); \
    abort(); } } while (0)
#define OK(x) ASSERT((x) == 0)
#define CALL(rt, sym, a) ios27_21i_dispatch(rt, "_pthread_" sym, a, 0, 0)
#define BASE UINT64_C(0x100000000)
enum { THREADS = 8, CYCLES = 350, MAX_ATTEMPTS = 100000 };

typedef struct {
    ios27_21i_runtime *runtime;
    pthread_barrier_t *barrier;
    uint64_t address;
    unsigned completed, contention;
} worker_state;

static void *worker(void *argument) {
    worker_state *state = argument;
    (void)pthread_barrier_wait(state->barrier);
    for (unsigned cycle = 0; cycle < CYCLES; ++cycle) {
        int rc = EAGAIN;
        for (unsigned attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
            rc = CALL(state->runtime, "mutex_init", state->address);
            if (rc == 0) break;
            ASSERT(rc == EAGAIN);
            ++state->contention;
            if ((attempt & 31u) == 0) ASSERT(sched_yield() == 0);
        }
        ASSERT(rc == 0);
        ios27_21i_token token = {0, 0};
        OK(ios27_21i_lookup(state->runtime, state->address,
                           IOS27_21I_MUTEX, &token));
        OK(ios27_21i_verify(state->runtime, state->address,
                            IOS27_21I_MUTEX, token));
        OK(CALL(state->runtime, "mutex_lock", state->address));
        ASSERT(CALL(state->runtime, "mutex_trylock", state->address) == EBUSY);
        OK(CALL(state->runtime, "mutex_unlock", state->address));
        OK(CALL(state->runtime, "mutex_destroy", state->address));
        ASSERT(ios27_21i_verify(state->runtime, state->address,
                                IOS27_21I_MUTEX, token) == EINVAL);
        ++state->completed;
    }
    return NULL;
}

static void concurrent_recycling(void) {
    uint8_t memory[4096] = {0};
    ios27_21i_guest guest = {memory, sizeof(memory), BASE, 64, 48, 8, 8};
    ios27_21i_runtime *runtime = NULL;
    pthread_barrier_t barrier;
    pthread_t threads[THREADS];
    worker_state workers[THREADS];
    ios27_21i_stats stats;
    OK(ios27_21i_create(&runtime, &guest, 1));
    OK(pthread_barrier_init(&barrier, NULL, THREADS));
    for (unsigned i = 0; i < THREADS; ++i) {
        workers[i] = (worker_state){runtime, &barrier, BASE + 64u + i * 128u,
                                    0, 0};
        OK(pthread_create(&threads[i], NULL, worker, &workers[i]));
    }
    unsigned transitions = 0, retries = 0;
    for (unsigned i = 0; i < THREADS; ++i) {
        OK(pthread_join(threads[i], NULL));
        transitions += workers[i].completed;
        retries += workers[i].contention;
    }
    OK(pthread_barrier_destroy(&barrier));
    ASSERT(transitions == THREADS * CYCLES);
    OK(ios27_21i_get_stats(runtime, &stats));
    ASSERT(stats.capacity == 1 && stats.mutexes == 0 &&
           stats.conditions == 0 && stats.in_flight == 0 &&
           stats.retired_slots == 0);
    OK(ios27_21i_destroy(runtime));
    printf("phase21i: %u concurrent allocation/destroy cycles, %u bounded quota retries, no leaked references\\n",
           transitions, retries);
}

static void high_address_boundaries(void) {
    uint8_t memory[128] = {0};
    const uint64_t base = UINT64_MAX - UINT64_C(135);
    ios27_21i_guest guest = {memory, sizeof(memory), base, 64, 48, 8, 8};
    ios27_21i_runtime *runtime = NULL;
    ios27_21i_token previous = {0, 0}, current = {0, 0};
    ios27_21i_stats stats;
    ASSERT((base & UINT64_C(7)) == 0);
    OK(ios27_21i_create(&runtime, &guest, 2));
    ASSERT(CALL(runtime, "mutex_init", UINT64_MAX) == EINVAL);
    ASSERT(CALL(runtime, "mutex_init", base + 120u) == EFAULT);
    OK(CALL(runtime, "cond_init", base));
    OK(CALL(runtime, "mutex_init", base + 64u));
    ASSERT(CALL(runtime, "mutex_init", base + 32u) == EBUSY);
    OK(ios27_21i_lookup(runtime, base + 64u, IOS27_21I_MUTEX, &previous));
    OK(CALL(runtime, "mutex_lock", base + 64u));
    ASSERT(CALL(runtime, "mutex_destroy", base + 64u) == EBUSY);
    OK(CALL(runtime, "mutex_unlock", base + 64u));
    OK(CALL(runtime, "mutex_destroy", base + 64u));
    ASSERT(ios27_21i_verify(runtime, base + 64u,
                            IOS27_21I_MUTEX, previous) == EINVAL);
    OK(CALL(runtime, "mutex_init", base + 64u));
    OK(ios27_21i_lookup(runtime, base + 64u, IOS27_21I_MUTEX, &current));
    ASSERT(current.generation != previous.generation);
    OK(CALL(runtime, "mutex_destroy", base + 64u));
    OK(CALL(runtime, "cond_destroy", base));
    OK(ios27_21i_get_stats(runtime, &stats));
    ASSERT(!stats.mutexes && !stats.conditions && !stats.in_flight);
    OK(ios27_21i_destroy(runtime));
    puts("phase21i: near-UINT64_MAX aligned guests, overflow rejection and stale token transitions passed");
}
int main(void) {
    concurrent_recycling();
    high_address_boundaries();
    return 0;
}
