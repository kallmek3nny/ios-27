#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21i_abi.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(e) do { if (!(e)) { \
    fprintf(stderr, "phase21i destroy races %s:%d: %s\\n", \
            __FILE__, __LINE__, #e); abort(); \
} } while (0)
#define OK(e) CHECK((e) == 0)
#define BASE UINT64_C(0x800000000)
#define M (BASE + 64u)
#define READY (BASE + 192u)
#define WAITING (BASE + 320u)
#define DO(rt, name, a, b) \
    ios27_21i_dispatch(rt, "_pthread_" name, a, b, 0)
enum { WORKERS = 16, ROUNDS = 96, DESTRUCT_TRIES = 256 };

typedef struct {
    ios27_21i_runtime *rt;
    unsigned arrived, released;
    int go;
} round_state;
static void cleanup_unlock(void *arg) {
    round_state *s = arg;
    OK(DO(s->rt, "mutex_unlock", M, 0));
}
static void *waiter(void *arg) {
    round_state *s = arg;
    OK(DO(s->rt, "mutex_lock", M, 0));
    pthread_cleanup_push(cleanup_unlock, s);
    ++s->arrived;
    OK(DO(s->rt, "cond_signal", READY, 0));
    while (!s->go)
        OK(DO(s->rt, "cond_wait", WAITING, M));
    ++s->released;
    pthread_cleanup_pop(1);
    return NULL;
}

typedef struct {
    round_state *state;
    unsigned calls;
} destructor_state;
static void *attempt_destroy(void *arg) {
    destructor_state *d = arg;
    for (unsigned i = 0; i < DESTRUCT_TRIES; ++i) {
        CHECK(DO(d->state->rt, "cond_destroy", WAITING, 0) == EBUSY);
        CHECK(DO(d->state->rt, "mutex_destroy", M, 0) == EBUSY);
        CHECK(ios27_21i_destroy(d->state->rt) == EBUSY);
        ++d->calls;
    }
    return NULL;
}
int main(void) {
    uint8_t guest_memory[1024] = {0};
    ios27_21i_guest guest = {guest_memory, sizeof(guest_memory),
                             BASE, 64, 48, 8, 8};
    ios27_21i_runtime *rt = NULL;
    ios27_21i_token previous = {0, 0}, current;
    ios27_21i_stats stats;
    unsigned busy_checks = 0;
    OK(ios27_21i_create(&rt, &guest, 3));
    for (unsigned round = 0; round < ROUNDS; ++round) {
        round_state state = {rt, 0, 0, 0};
        pthread_t threads[WORKERS], destroyer;
        destructor_state destroy = {&state, 0};
        OK(DO(rt, "mutex_init", M, 0));
        OK(DO(rt, "cond_init", READY, 0));
        OK(DO(rt, "cond_init", WAITING, 0));
        OK(ios27_21i_lookup(rt, M, IOS27_21I_MUTEX, &current));
        if (round) {
            CHECK(ios27_21i_verify(rt, M, IOS27_21I_MUTEX, previous)
                  == EINVAL);
            CHECK(current.generation > previous.generation);
        }
        for (unsigned i = 0; i < WORKERS; ++i)
            OK(pthread_create(&threads[i], NULL, waiter, &state));
        OK(DO(rt, "mutex_lock", M, 0));
        while (state.arrived != WORKERS)
            OK(DO(rt, "cond_wait", READY, M));
        OK(ios27_21i_get_stats(rt, &stats));
        CHECK(stats.in_flight == WORKERS * 2u);
        CHECK(stats.mutexes == 1 && stats.conditions == 2);
        OK(pthread_create(&destroyer, NULL, attempt_destroy, &destroy));
        OK(pthread_join(destroyer, NULL));
        CHECK(destroy.calls == DESTRUCT_TRIES);
        busy_checks += destroy.calls * 3u;
        state.go = 1;
        OK(DO(rt, "cond_broadcast", WAITING, 0));
        OK(DO(rt, "mutex_unlock", M, 0));
        for (unsigned i = 0; i < WORKERS; ++i)
            OK(pthread_join(threads[i], NULL));
        CHECK(state.released == WORKERS);
        OK(ios27_21i_get_stats(rt, &stats));
        CHECK(!stats.in_flight);
        OK(DO(rt, "cond_destroy", WAITING, 0));
        OK(DO(rt, "cond_destroy", READY, 0));
        OK(DO(rt, "mutex_destroy", M, 0));
        CHECK(ios27_21i_verify(rt, M, IOS27_21I_MUTEX, current)
              == EINVAL);
        previous = current;
    }
    OK(ios27_21i_get_stats(rt, &stats));
    CHECK(!stats.mutexes && !stats.conditions && !stats.in_flight &&
          !stats.retired_slots);
    OK(ios27_21i_destroy(rt));
    printf("phase21i: %u synchronized waiters across %u cycles, %u competing destroy attempts, no reused live objects or leaked references\\n",
           WORKERS, ROUNDS, busy_checks);
    return 0;
}
