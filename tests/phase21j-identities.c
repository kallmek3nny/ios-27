#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21j_diagnostics.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21j identities:%d %s\n", __LINE__, #x); abort(); \
} } while (0)
enum { ACTORS = 8, ROUNDS = 512, TOTAL = ACTORS * ROUNDS };
typedef struct { unsigned actor; uint64_t *identities; } arg;
static void *worker(void *pointer) {
    arg *a = pointer;
    for (unsigned i = 0; i < ROUNDS; ++i) {
        ios27_21j_diagnostics *d = NULL;
        ios27_21j_lease handle;
        ios27_21j_snapshot_data stats;
        CHECK(ios27_21j_create(&d, 1, (uint64_t)i, 0) == 0);
        CHECK(ios27_21j_acquire(d, &handle) == 0);
        CHECK(handle.owner != 0 && handle.slot == 1 &&
              handle.generation == 1);
        a->identities[a->actor * ROUNDS + i] = handle.owner;
        CHECK(ios27_21j_release(d, handle) == 0);
        CHECK(ios27_21j_release(d, handle) == EINVAL);
        CHECK(ios27_21j_snapshot(d, &stats) == 0);
        CHECK(stats.active == 0 && stats.peak == 1 &&
              stats.leases_created == stats.leases_released);
        CHECK(ios27_21j_destroy(d) == 0);
    }
    return NULL;
}
static int compare_ids(const void *left, const void *right) {
    uint64_t a = *(const uint64_t *)left;
    uint64_t b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}
int main(void) {
    uint64_t identities[TOTAL] = {0};
    pthread_t threads[ACTORS];
    arg args[ACTORS];
    ios27_21j_diagnostics *a = NULL, *b = NULL;
    ios27_21j_lease stale, fresh;
    CHECK(ios27_21j_create(&a, 1, 0, 0) == 0);
    CHECK(ios27_21j_acquire(a, &stale) == 0);
    CHECK(ios27_21j_release(a, stale) == 0);
    CHECK(ios27_21j_destroy(a) == 0);
    CHECK(ios27_21j_create(&b, 1, 0, 0) == 0);
    CHECK(ios27_21j_acquire(b, &fresh) == 0);
    CHECK(stale.owner != fresh.owner);
    CHECK(ios27_21j_release(b, stale) == EINVAL);
    CHECK(ios27_21j_release(b, fresh) == 0);
    CHECK(ios27_21j_destroy(b) == 0);
    for (unsigned actor = 0; actor < ACTORS; ++actor) {
        args[actor] = (arg){actor, identities};
        CHECK(pthread_create(&threads[actor], NULL, worker, &args[actor]) == 0);
    }
    for (unsigned actor = 0; actor < ACTORS; ++actor)
        CHECK(pthread_join(threads[actor], NULL) == 0);
    qsort(identities, TOTAL, sizeof(identities[0]), compare_ids);
    CHECK(identities[0] != 0);
    for (unsigned i = 1; i < TOTAL; ++i)
        CHECK(identities[i] > identities[i - 1u]);
    printf("phase21j identities: %u concurrent runtime generations with no identity alias or leaked lease; PASS\n",
           TOTAL);
    return 0;
}
