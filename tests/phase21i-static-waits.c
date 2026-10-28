#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21i_abi.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21i static wait %s:%d: %s\\n", \
            __FILE__, __LINE__, #x); abort(); \
} } while (0)
#define OK(x) CHECK((x) == 0)
#define BASE UINT64_C(0xb00000000)
#define M (BASE + 64u)
#define READY (BASE + 192u)
#define C (BASE + 320u)
#define DISPATCH(rt, symbol, a, b) \
    ios27_21i_dispatch(rt, "_pthread_" symbol, a, b, 0)
enum { ROUNDS = 64, WAITERS = 12 };
typedef struct {
    ios27_21i_runtime *rt;
    unsigned entered, finished;
    int release;
} scene;
static void put64(uint8_t *bytes, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) bytes[i] = (uint8_t)(value >> (i*8u));
}
static uint64_t get64(const uint8_t *bytes) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= (uint64_t)bytes[i] << (i*8u);
    return value;
}
static void unlock(void *arg) {
    scene *s = arg;
    OK(DISPATCH(s->rt, "mutex_unlock", M, 0));
}
static void *consumer(void *arg) {
    scene *s = arg;
    OK(DISPATCH(s->rt, "mutex_lock", M, 0));
    pthread_cleanup_push(unlock, s);
    ++s->entered;
    OK(DISPATCH(s->rt, "cond_signal", READY, 0));
    while (!s->release)
        OK(DISPATCH(s->rt, "cond_wait", C, M));
    ++s->finished;
    pthread_cleanup_pop(1);
    return NULL;
}
int main(void) {
    uint8_t memory[1024] = {0};
    ios27_21i_guest guest = {memory, sizeof(memory), BASE, 64, 48, 8, 8};
    ios27_21i_runtime *rt = NULL;
    ios27_21i_stats stats;
    unsigned cancelled = 0;
    OK(ios27_21i_create_apple_lp64_attrs(&rt, &guest, 3));
    for (unsigned round = 0; round < ROUNDS; ++round) {
        scene state = {rt, 0, 0, 0};
        pthread_t workers[WAITERS];
        void *join_result;
        int should_cancel = round % 4u == 0u;
        put64(memory + 64, UINT64_C(0x32aaaba7));
        put64(memory + 192, UINT64_C(0x3cb0b1bb));
        put64(memory + 320, UINT64_C(0x3cb0b1bb));
        for (unsigned i = 0; i < WAITERS; ++i)
            OK(pthread_create(&workers[i], NULL, consumer, &state));
        OK(DISPATCH(rt, "mutex_lock", M, 0));
        while (state.entered < WAITERS)
            OK(DISPATCH(rt, "cond_wait", READY, M));
        OK(ios27_21i_get_stats(rt, &stats));
        CHECK(stats.mutexes == 1 && stats.conditions == 2 &&
              stats.in_flight == 2u * WAITERS);
        CHECK(DISPATCH(rt, "cond_destroy", C, 0) == EBUSY);
        if (should_cancel) {
            /* Keep the producer mutex held while selecting a waiting thread;
             * then allow POSIX cancellation to reacquire it for cleanup. */
            OK(pthread_cancel(workers[0]));
            OK(DISPATCH(rt, "mutex_unlock", M, 0));
            OK(pthread_join(workers[0], &join_result));
            CHECK(join_result == PTHREAD_CANCELED);
            ++cancelled;
            OK(DISPATCH(rt, "mutex_lock", M, 0));
        }
        state.release = 1;
        OK(DISPATCH(rt, "cond_broadcast", C, 0));
        OK(DISPATCH(rt, "mutex_unlock", M, 0));
        for (unsigned i = should_cancel ? 1u : 0u; i < WAITERS; ++i)
            OK(pthread_join(workers[i], NULL));
        CHECK(state.finished == WAITERS - (unsigned)should_cancel);
        OK(ios27_21i_get_stats(rt, &stats));
        CHECK(!stats.in_flight);
        OK(DISPATCH(rt, "cond_destroy", C, 0));
        OK(DISPATCH(rt, "cond_destroy", READY, 0));
        OK(DISPATCH(rt, "mutex_destroy", M, 0));
        CHECK(get64(memory + 64) == 0 && get64(memory + 192) == 0 &&
              get64(memory + 320) == 0);
        CHECK(DISPATCH(rt, "mutex_lock", M, 0) == EINVAL);
    }
    OK(ios27_21i_get_stats(rt, &stats));
    CHECK(!stats.mutexes && !stats.conditions && !stats.in_flight);
    OK(ios27_21i_destroy(rt));
    printf("phase21i: %u static conditional wait rounds with %u waiters each, %u mid-wait cancellations, zero stale object resurrection\\n",
           ROUNDS, WAITERS, cancelled);
    return 0;
}
