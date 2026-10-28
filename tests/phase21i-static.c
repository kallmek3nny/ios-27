#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21i_abi.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21i static %s:%d: %s\\n", \
            __FILE__, __LINE__, #x); abort(); \
} } while (0)
#define OK(x) CHECK((x) == 0)
#define BASE UINT64_C(0x900000000)
#define M (BASE + 64u)
#define C (BASE + 192u)
#define E (BASE + 320u)
#define R (BASE + 448u)
#define FIRSTFIT (BASE + 576u)
#define INVALID (BASE + 704u)
#define CALL(rt, sym, address) \
    ios27_21i_dispatch(rt, "_pthread_" sym, address, 0, 0)
enum { WORKERS = 16, STEPS = 2000 };
static void put_le64(uint8_t *p, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) p[i] = (uint8_t)(value >> (8u*i));
}
static uint64_t get_le64(const uint8_t *p) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= (uint64_t)p[i] << (8u*i);
    return value;
}
typedef struct {
    ios27_21i_runtime *rt;
    pthread_barrier_t *barrier;
    unsigned *count;
} counter_state;
static void *counter(void *arg) {
    counter_state *s = arg;
    (void)pthread_barrier_wait(s->barrier);
    for (unsigned i = 0; i < STEPS; ++i) {
        OK(CALL(s->rt, "mutex_lock", M));
        ++*s->count;
        OK(CALL(s->rt, "mutex_unlock", M));
    }
    return NULL;
}
static void test_static(void) {
    uint8_t memory[2048] = {0};
    ios27_21i_guest guest = {memory, sizeof(memory), BASE, 64, 48, 8, 8};
    ios27_21i_runtime *rt = NULL;
    ios27_21i_token old, fresh;
    ios27_21i_stats stats;
    pthread_t workers[WORKERS];
    pthread_barrier_t start;
    unsigned count = 0;
    counter_state state;
    /* Source Apple libpthread pthread_impl.h:
     * 32AAABA7 default; 32AAABA1 errorcheck; 32AAABA2 recursive;
     * 32AAABA3 firstfit; 3CB0B1BB condition. */
    put_le64(memory + 64, UINT64_C(0x32aaaba7));
    put_le64(memory + 192, UINT64_C(0x3cb0b1bb));
    put_le64(memory + 320, UINT64_C(0x32aaaba1));
    put_le64(memory + 448, UINT64_C(0x32aaaba2));
    put_le64(memory + 576, UINT64_C(0x32aaaba3));
    put_le64(memory + 704, UINT64_C(0x32aaaba7));
    memory[704 + 63] = 1; /* malformed initializer has nonzero opaque tail */
    OK(ios27_21i_create(&rt, &guest, 6));
    CHECK(CALL(rt, "mutex_lock", M) == EINVAL);
    CHECK(CALL(rt, "cond_signal", C) == EINVAL);
    OK(ios27_21i_destroy(rt));
    OK(ios27_21i_create_apple_lp64_attrs(&rt, &guest, 6));
    CHECK(CALL(rt, "mutex_lock", INVALID) == EINVAL);
    OK(ios27_21i_get_stats(rt, &stats));
    CHECK(!stats.mutexes && !stats.conditions);
    OK(pthread_barrier_init(&start, NULL, WORKERS));
    state = (counter_state){rt, &start, &count};
    for (unsigned i = 0; i < WORKERS; ++i)
        OK(pthread_create(&workers[i], NULL, counter, &state));
    for (unsigned i = 0; i < WORKERS; ++i) OK(pthread_join(workers[i], NULL));
    OK(pthread_barrier_destroy(&start));
    CHECK(count == WORKERS * STEPS);
    OK(ios27_21i_get_stats(rt, &stats));
    CHECK(stats.mutexes == 1 && stats.conditions == 0 && !stats.in_flight);
    OK(ios27_21i_lookup(rt, M, IOS27_21I_MUTEX, &old));
    /* Static object activation is distinct from guest explicit init. */
    CHECK(CALL(rt, "mutex_init", M) == EBUSY);
    OK(CALL(rt, "cond_signal", C));
    OK(CALL(rt, "cond_broadcast", C));
    OK(CALL(rt, "mutex_lock", E));
    CHECK(CALL(rt, "mutex_lock", E) == EDEADLK);
    OK(CALL(rt, "mutex_unlock", E));
    OK(CALL(rt, "mutex_lock", R));
    OK(CALL(rt, "mutex_lock", R));
    OK(CALL(rt, "mutex_unlock", R));
    OK(CALL(rt, "mutex_unlock", R));
    OK(CALL(rt, "mutex_lock", FIRSTFIT));
    OK(CALL(rt, "mutex_unlock", FIRSTFIT));
    CHECK(CALL(rt, "mutexattr_init", M) == EBUSY);
    OK(CALL(rt, "mutex_destroy", FIRSTFIT));
    OK(CALL(rt, "mutex_destroy", R));
    OK(CALL(rt, "mutex_destroy", E));
    OK(CALL(rt, "cond_destroy", C));
    CHECK(get_le64(memory + 192) == 0);
    CHECK(CALL(rt, "cond_signal", C) == EINVAL);
    OK(CALL(rt, "mutex_destroy", M));
    CHECK(get_le64(memory + 64) == 0);
    CHECK(CALL(rt, "mutex_lock", M) == EINVAL);
    CHECK(ios27_21i_verify(rt, M, IOS27_21I_MUTEX, old) == EINVAL);
    /* A new, explicit re-initialization is now necessary, not automatic. */
    put_le64(memory + 64, UINT64_C(0x32aaaba7));
    OK(CALL(rt, "mutex_lock", M));
    OK(ios27_21i_lookup(rt, M, IOS27_21I_MUTEX, &fresh));
    CHECK(fresh.generation > old.generation);
    OK(CALL(rt, "mutex_unlock", M));
    OK(CALL(rt, "mutex_destroy", M));
    OK(ios27_21i_get_stats(rt, &stats));
    CHECK(!stats.mutexes && !stats.conditions && !stats.in_flight);
    CHECK(get_le64(memory + 320) == 0 && get_le64(memory + 448) == 0 &&
          get_le64(memory + 576) == 0);
    OK(ios27_21i_destroy(rt));
    puts("phase21i: Apple-published static mutex/condition patterns, 32k contended lazy transitions, signature rejection, generation-safe destruction passed");
}
int main(void) { test_static(); return 0; }
