#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21f_mutex.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define ENSURE(condition) do { if (!(condition)) { \
    fprintf(stderr, "phase21f isolation %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    abort(); } } while (0)
enum { WORKERS = 6, ROUNDS = 500 };
static uint64_t ids[WORKERS * ROUNDS * 2];
typedef struct { unsigned index; } worker_args;
static void *worker(void *arg) {
    worker_args *w = arg;
    for (unsigned i = 0; i < ROUNDS; ++i) {
        ios27_21f_runtime *a = NULL, *b = NULL;
        ios27_21f_handle ha, hb, forged;
        ios27_21f_info info;
        ENSURE(ios27_21f_runtime_create(&a, 1) == 0);
        ENSURE(ios27_21f_runtime_create(&b, 1) == 0);
        ENSURE(ios27_21f_mutex_create(a, IOS27_21F_NORMAL, &ha) == 0);
        ENSURE(ios27_21f_mutex_create(b, IOS27_21F_NORMAL, &hb) == 0);
        ENSURE(ha.slot == hb.slot && ha.generation == hb.generation);
        ENSURE(ha.runtime_id != hb.runtime_id && ha.runtime_id && hb.runtime_id);
        ids[(w->index * ROUNDS + i) * 2] = ha.runtime_id;
        ids[(w->index * ROUNDS + i) * 2 + 1] = hb.runtime_id;
        ENSURE(ios27_21f_mutex_lock(b, ha) == EINVAL);
        ENSURE(ios27_21f_mutex_trylock(b, ha) == EINVAL);
        ENSURE(ios27_21f_mutex_unlock(b, ha) == EINVAL);
        ENSURE(ios27_21f_mutex_info(b, ha, &info) == EINVAL);
        ENSURE(ios27_21f_mutex_destroy(b, ha) == EINVAL);
        ENSURE(ios27_21f_mutex_lock(a, hb) == EINVAL);
        forged = ha;
        forged.runtime_id ^= UINT64_C(1);
        ENSURE(ios27_21f_mutex_lock(a, forged) == EINVAL);
        ENSURE(ios27_21f_mutex_lock(a, ha) == 0);
        ENSURE(ios27_21f_mutex_trylock(b, hb) == 0);
        ENSURE(ios27_21f_mutex_unlock(b, hb) == 0);
        ENSURE(ios27_21f_mutex_unlock(a, ha) == 0);
        ENSURE(ios27_21f_mutex_destroy(a, ha) == 0);
        ENSURE(ios27_21f_mutex_destroy(b, hb) == 0);
        ENSURE(ios27_21f_runtime_destroy(a) == 0);
        ENSURE(ios27_21f_runtime_destroy(b) == 0);
    }
    return NULL;
}
static int compare_ids(const void *a, const void *b) {
    uint64_t aa = *(const uint64_t *)a, bb = *(const uint64_t *)b;
    return (aa > bb) - (aa < bb);
}
int main(void) {
    pthread_t threads[WORKERS];
    worker_args args[WORKERS];
    for (unsigned i = 0; i < WORKERS; ++i) {
        args[i].index = i;
        ENSURE(pthread_create(&threads[i], NULL, worker, &args[i]) == 0);
    }
    for (unsigned i = 0; i < WORKERS; ++i)
        ENSURE(pthread_join(threads[i], NULL) == 0);
    qsort(ids, WORKERS * ROUNDS * 2, sizeof(ids[0]), compare_ids);
    ENSURE(ids[0] > 0);
    for (size_t i = 1; i < WORKERS * ROUNDS * 2; ++i)
        ENSURE(ids[i] > ids[i - 1]);
    puts("phase21f isolation: 6000 concurrent runtime identities and cross-runtime rejection passed");
    return 0;
}
