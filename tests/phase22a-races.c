#include "ios27/phase22a_space.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define READERS 6u
#define ROUNDS 16000u
#define CHECK(x) assert((x) == IOS27_22A_OK)

typedef struct {
    ios27_22a_space *space;
    unsigned id;
    unsigned closed_observed;
} worker;

static void *reader(void *opaque) {
    worker *w = opaque;
    uint8_t data[64];
    for (unsigned i = 0; i < ROUNDS; ++i) {
        int result = ios27_22a_copyin(w->space, data, 0x8000u, sizeof(data));
        if (result == IOS27_22A_CLOSED) { ++w->closed_observed; break; }
        assert(result == IOS27_22A_OK);
        if ((i & 15u) == 0u) {
            ios27_22a_stats stats;
            CHECK(ios27_22a_snapshot(w->space, &stats));
            assert(stats.regions <= 1u);
        }
    }
    return NULL;
}

int main(void) {
    ios27_22a_config cfg = {0x20000u, 4096u, 1u};
    ios27_22a_space *s = NULL;
    pthread_t threads[READERS];
    worker workers[READERS];
    ios27_22a_stats stats;
    uint64_t handle;
    CHECK(ios27_22a_create(&cfg, &s));
    CHECK(ios27_22a_map(s, 0x8000u, 4096u, 3u, 3u, &handle));
    for (unsigned i = 0; i < READERS; ++i) {
        workers[i] = (worker){s, i, 0u};
        assert(pthread_create(&threads[i], NULL, reader, &workers[i]) == 0);
    }
    CHECK(ios27_22a_close(s));
    for (unsigned i = 0; i < READERS; ++i) assert(pthread_join(threads[i], NULL) == 0);
    CHECK(ios27_22a_snapshot(s, &stats));
    assert(stats.closed && !stats.regions && !stats.pins && !stats.committed);
    CHECK(ios27_22a_destroy(&s));
    printf("phase22a races: %u readers safely serialized with close\n", READERS);
    return 0;
}
