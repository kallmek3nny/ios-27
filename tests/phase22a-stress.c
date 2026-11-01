#include "ios27/phase22a_space.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WORKERS 4u
#define ROUNDS 30000u
#define CHECK(x) assert((x) == IOS27_22A_OK)

typedef struct {
    ios27_22a_space *space;
    unsigned id;
    uint64_t operations;
    uint64_t checksum;
} worker;

static uint64_t next_random(uint64_t *seed) {
    *seed ^= *seed << 13u;
    *seed ^= *seed >> 7u;
    *seed ^= *seed << 17u;
    return *seed;
}

static void *run(void *opaque) {
    worker *w = opaque;
    uint64_t state = UINT64_C(0x5eadbeef) + (uint64_t)w->id * 131u;
    uint64_t base = 0x10000u + (uint64_t)w->id * 0x20000u;
    uint64_t handle = 0u;
    for (unsigned i = 0; i < ROUNDS; ++i) {
        uint64_t random = next_random(&state);
        uint8_t src[32], dst[32];
        uint64_t at = base + ((random >> 11u) % (4096u - sizeof(src)));
        memset(src, (unsigned char)(random ^ (random >> 27u)), sizeof(src));
        if ((i % 31u) == 0u) {
            if (handle) CHECK(ios27_22a_unmap(w->space, handle));
            CHECK(ios27_22a_map(w->space, base, IOS27_22A_PAGE, 3u, 3u, &handle));
            ++w->operations;
        }
        CHECK(ios27_22a_copyout(w->space, at, src, sizeof(src)));
        CHECK(ios27_22a_copyin(w->space, dst, at, sizeof(dst)));
        assert(memcmp(src, dst, sizeof(src)) == 0);
        w->operations += 2u;
        w->checksum += dst[(random >> 19u) & 31u];
        if ((i % 5u) == 0u) {
            ios27_22a_view view;
            CHECK(ios27_22a_pin(w->space, at, sizeof(src), 1u, 3u, &view));
            assert(memcmp(view.host, src, sizeof(src)) == 0);
            CHECK(ios27_22a_unpin(w->space, &view));
            w->operations += 2u;
        }
        if ((i % 7u) == 0u) {
            assert(ios27_22a_probe(w->space, base - 1u, 1u, 1u, 1u) == IOS27_22A_FAULT);
            ++w->operations;
        }
    }
    CHECK(ios27_22a_unmap(w->space, handle));
    ++w->operations;
    return NULL;
}

int main(void) {
    ios27_22a_config cfg = {0x100000u, 65536u, 8u};
    ios27_22a_space *space = NULL;
    pthread_t threads[WORKERS];
    worker workers[WORKERS];
    ios27_22a_stats snapshot;
    uint64_t total = 0u, checksum = 0u;
    CHECK(ios27_22a_create(&cfg, &space));
    for (unsigned i = 0; i < WORKERS; ++i) {
        workers[i] = (worker){space, i, 0u, 0u};
        assert(pthread_create(&threads[i], NULL, run, &workers[i]) == 0);
    }
    for (unsigned i = 0; i < WORKERS; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
        total += workers[i].operations;
        checksum += workers[i].checksum;
    }
    CHECK(ios27_22a_snapshot(space, &snapshot));
    assert(total == 309020u && checksum == 15291755u);
    assert(snapshot.regions == 0u && snapshot.committed == 0u && snapshot.pins == 0u);
    CHECK(ios27_22a_destroy(&space));
    printf("phase22a stress: %llu operations across %u threads, checksum=%llu\n",
           (unsigned long long)total, WORKERS, (unsigned long long)checksum);
    return 0;
}
