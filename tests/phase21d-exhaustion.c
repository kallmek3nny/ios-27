#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21d_rw_once.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(call) do { int rc_ = (call); if (rc_) { \
    fprintf(stderr, "%s:%d: %s => %d\n", __FILE__, __LINE__, #call, rc_); abort(); \
} } while (0)
#define REQUIRE(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: failed %s\n", __FILE__, __LINE__, #condition); abort(); \
} } while (0)

static int init_once(void *context) {
    unsigned *count = context;
    ++*count;
    return 0;
}
static void test_argument_boundaries(void) {
    ios27_21d_runtime *rt = NULL;
    ios27_21d_handle h = {0};
    REQUIRE(ios27_21d_runtime_create(NULL, 1) == EINVAL);
    REQUIRE(ios27_21d_runtime_create(&rt, 0) == EINVAL);
    REQUIRE(ios27_21d_runtime_create(&rt, IOS27_21D_MAX_OBJECTS + 1) == EINVAL);
    REQUIRE(rt == NULL);
    REQUIRE(ios27_21d_runtime_destroy(NULL) == EINVAL);
    CHECK(ios27_21d_runtime_create(&rt, 1));
    REQUIRE(ios27_21d_create(rt, IOS27_21D_RWLOCK, NULL) == EINVAL);
    REQUIRE(ios27_21d_create(rt, (enum ios27_21d_kind)-1, &h) == EINVAL);
    REQUIRE(ios27_21d_create(rt, (enum ios27_21d_kind)9999, &h) == EINVAL);
    REQUIRE(ios27_21d_rdlock(NULL, h) == EINVAL);
    REQUIRE(ios27_21d_wrlock(rt, h) == EINVAL);
    REQUIRE(ios27_21d_unlock(rt, h) == EINVAL);
    REQUIRE(ios27_21d_destroy(rt, h) == EINVAL);
    REQUIRE(ios27_21d_once(rt, h, init_once, NULL) == EINVAL);
    REQUIRE(ios27_21d_usage(NULL, NULL, NULL, NULL) == EINVAL);
    CHECK(ios27_21d_runtime_destroy(rt));
}

static void test_full_pool_and_reuse(void) {
    ios27_21d_runtime *rt;
    ios27_21d_handle handles[IOS27_21D_MAX_OBJECTS], extra, old;
    unsigned calls = 0;
    size_t objects = 0, waiting = 0, holders = 0;
    CHECK(ios27_21d_runtime_create(&rt, IOS27_21D_MAX_OBJECTS));
    for (size_t i = 0; i < IOS27_21D_MAX_OBJECTS; ++i) {
        enum ios27_21d_kind kind = (i % 2u) ? IOS27_21D_ONCE : IOS27_21D_RWLOCK;
        CHECK(ios27_21d_create(rt, kind, &handles[i]));
        REQUIRE(handles[i].slot == i + 1u && handles[i].runtime_cookie);
        if (kind == IOS27_21D_ONCE) CHECK(ios27_21d_once(rt, handles[i], init_once, &calls));
    }
    CHECK(ios27_21d_usage(rt, &objects, &waiting, &holders));
    REQUIRE(objects == IOS27_21D_MAX_OBJECTS && waiting == 0 && holders == 0);
    extra = handles[0];
    REQUIRE(ios27_21d_create(rt, IOS27_21D_RWLOCK, &extra) == EAGAIN);
    REQUIRE(extra.slot == 0 && extra.generation == 0 && extra.runtime_cookie == 0);
    REQUIRE(ios27_21d_runtime_destroy(rt) == EBUSY);
    REQUIRE(calls == IOS27_21D_MAX_OBJECTS / 2u);
    for (size_t i = 0; i < IOS27_21D_MAX_OBJECTS; ++i) {
        ios27_21d_handle bad = handles[i];
        bad.generation++;
        REQUIRE(ios27_21d_destroy(rt, bad) == EINVAL);
        bad = handles[i];
        ++bad.runtime_cookie;
        REQUIRE(ios27_21d_destroy(rt, bad) == EINVAL);
    }
    old = handles[255];
    for (size_t i = 0; i < IOS27_21D_MAX_OBJECTS; ++i)
        CHECK(ios27_21d_destroy(rt, handles[i]));
    CHECK(ios27_21d_create(rt, IOS27_21D_RWLOCK, &extra));
    REQUIRE(extra.slot == 1u);
    REQUIRE(extra.generation != handles[0].generation);
    REQUIRE(ios27_21d_rdlock(rt, handles[0]) == EINVAL);
    REQUIRE(ios27_21d_once(rt, old, init_once, &calls) == EINVAL);
    CHECK(ios27_21d_wrlock(rt, extra));
    REQUIRE(ios27_21d_destroy(rt, extra) == EBUSY);
    CHECK(ios27_21d_unlock(rt, extra));
    CHECK(ios27_21d_destroy(rt, extra));
    CHECK(ios27_21d_runtime_destroy(rt));
}

static void test_runtime_cookie_churn(void) {
    ios27_21d_handle previous = {0};
    for (int round = 0; round < 2048; ++round) {
        ios27_21d_runtime *rt;
        ios27_21d_handle current;
        CHECK(ios27_21d_runtime_create(&rt, 1));
        CHECK(ios27_21d_create(rt, IOS27_21D_RWLOCK, &current));
        REQUIRE(current.runtime_cookie > previous.runtime_cookie);
        if (round != 0) {
            REQUIRE(ios27_21d_tryrdlock(rt, previous) == EINVAL);
            REQUIRE(ios27_21d_destroy(rt, previous) == EINVAL);
        }
        CHECK(ios27_21d_destroy(rt, current));
        CHECK(ios27_21d_runtime_destroy(rt));
        previous = current;
    }
}

int main(void) {
    test_argument_boundaries();
    test_full_pool_and_reuse();
    test_runtime_cookie_churn();
    puts("phase21d: 256-slot exhaustion, 2048 runtime cookies, invalid handles and generation rejection");
    return 0;
}
