/* Z2 t-a6687b: host-side adversarial tests for the bounded libSystem handle
 * tables in src/libsystem21c_sync.c, src/libsystem21e_tls.c and
 * src/libsystem21g_cond.c. Every case is single-threaded and deterministic:
 * nothing blocks, starts a thread or executes native code. Every reject must
 * return its errno and leave the table usable: the remaining capacity can
 * still be created and live handles still work. Cases that tests/phase21c-*,
 * phase21e-* and phase21g-* already cover are not repeated here.
 *
 * Not covered: the REALTIME overflow guard in deadline_to_monotonic() in
 * src/libsystem21g_cond.c. It only fires while CLOCK_REALTIME is behind
 * CLOCK_MONOTONIC, and any far-future deadline that passes it blocks, so no
 * single-threaded case can reach it. */
#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21c_sync.h"
#include "ios27/libsystem21e_tls.h"
#include "ios27/libsystem21g_cond.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } \
} while (0)

static void setup_failed(const char *what) {
    fprintf(stderr, "FAIL setup: %s\n", what);
    ++failures;
}

/* ---- libsystem21g condition variables ---- */

/* test_g_stale_reuse fills the table with exactly four objects. */
#define G_CAP 4u

static int g_open(ios27_21g_runtime **rt, size_t cap, ios27_21g_handle *m,
                  ios27_21g_handle *c) {
    *rt = NULL;
    if (ios27_21g_runtime_create(rt, cap) != 0) {
        setup_failed("21g runtime_create");
        return -1;
    }
    if (ios27_21g_create(*rt, IOS27_21G_MUTEX, m) != 0 ||
        ios27_21g_create(*rt, IOS27_21G_COND, c) != 0) {
        setup_failed("21g live mutex and condition");
        return -1;
    }
    return 0;
}

static void g_close(ios27_21g_runtime *rt, ios27_21g_handle m,
                    ios27_21g_handle c) {
    CHECK(ios27_21g_destroy(rt, c) == 0);
    CHECK(ios27_21g_destroy(rt, m) == 0);
    CHECK(ios27_21g_runtime_destroy(rt) == 0);
}

/* Every handle-taking entry point must reject h with EINVAL. m and c are a
 * live, unowned mutex and condition, so each wait reaches the lookup of h. */
static void g_reject_everywhere(ios27_21g_runtime *rt, ios27_21g_handle h,
                                ios27_21g_handle m, ios27_21g_handle c) {
    struct timespec past = {0, 0};
    ios27_21g_stats st;
    CHECK(ios27_21g_mutex_lock(rt, h) == EINVAL);
    CHECK(ios27_21g_mutex_trylock(rt, h) == EINVAL);
    CHECK(ios27_21g_mutex_unlock(rt, h) == EINVAL);
    CHECK(ios27_21g_cond_signal(rt, h) == EINVAL);
    CHECK(ios27_21g_cond_broadcast(rt, h) == EINVAL);
    CHECK(ios27_21g_cond_stats(rt, h, &st) == EINVAL);
    CHECK(ios27_21g_cond_wait(rt, h, m) == EINVAL);
    CHECK(ios27_21g_cond_wait(rt, c, h) == EINVAL);
    CHECK(ios27_21g_cond_timedwait(rt, h, m, IOS27_21G_MONOTONIC, &past) ==
          EINVAL);
    CHECK(ios27_21g_cond_timedwait(rt, c, h, IOS27_21G_MONOTONIC, &past) ==
          EINVAL);
    CHECK(ios27_21g_destroy(rt, h) == EINVAL);
}

/* A live handle of the other kind must be rejected the same way. */
static void g_wrong_kind(ios27_21g_runtime *rt, ios27_21g_handle m,
                         ios27_21g_handle c) {
    struct timespec past = {0, 0};
    ios27_21g_stats st;
    CHECK(ios27_21g_mutex_lock(rt, c) == EINVAL);
    CHECK(ios27_21g_mutex_trylock(rt, c) == EINVAL);
    CHECK(ios27_21g_mutex_unlock(rt, c) == EINVAL);
    CHECK(ios27_21g_cond_signal(rt, m) == EINVAL);
    CHECK(ios27_21g_cond_broadcast(rt, m) == EINVAL);
    CHECK(ios27_21g_cond_stats(rt, m, &st) == EINVAL);
    CHECK(ios27_21g_cond_wait(rt, m, c) == EINVAL);
    CHECK(ios27_21g_cond_wait(rt, m, m) == EINVAL);
    CHECK(ios27_21g_cond_wait(rt, c, c) == EINVAL);
    CHECK(ios27_21g_cond_timedwait(rt, m, c, IOS27_21G_MONOTONIC, &past) ==
          EINVAL);
}

/* The table must still serve live handles and every one of its free slots. */
static void g_expect_usable(ios27_21g_runtime *rt, ios27_21g_handle m,
                            ios27_21g_handle c, size_t free_slots) {
    ios27_21g_handle extra[IOS27_21G_MAX_OBJECTS], spill;
    ios27_21g_stats st = {1u, 1u, 1u, 1u};
    size_t made = 0;
    CHECK(ios27_21g_mutex_lock(rt, m) == 0);
    CHECK(ios27_21g_mutex_unlock(rt, m) == 0);
    CHECK(ios27_21g_cond_signal(rt, c) == 0);
    CHECK(ios27_21g_cond_stats(rt, c, &st) == 0);
    CHECK(st.queued == 0 && st.outstanding == 0);
    CHECK(st.wakeups == 0 && st.timeouts == 0);
    while (made < free_slots && made < IOS27_21G_MAX_OBJECTS &&
           ios27_21g_create(rt, IOS27_21G_MUTEX, &extra[made]) == 0)
        ++made;
    CHECK(made == free_slots);
    CHECK(ios27_21g_create(rt, IOS27_21G_COND, &spill) == EAGAIN);
    while (made) {
        --made;
        CHECK(ios27_21g_destroy(rt, extra[made]) == 0);
    }
}

static void test_g_runtime_bounds(void) {
    ios27_21g_runtime *rt = NULL;
    ios27_21g_handle h[IOS27_21G_MAX_OBJECTS], spill;
    size_t made = 0;
    CHECK(ios27_21g_runtime_create(&rt, IOS27_21G_MAX_OBJECTS + 1u) == EINVAL);
    CHECK(ios27_21g_runtime_create(&rt, SIZE_MAX) == EINVAL);
    CHECK(ios27_21g_runtime_destroy(NULL) == EINVAL);
    if (ios27_21g_runtime_create(&rt, IOS27_21G_MAX_OBJECTS) != 0) {
        setup_failed("21g runtime at IOS27_21G_MAX_OBJECTS");
        return;
    }
    while (made < IOS27_21G_MAX_OBJECTS &&
           ios27_21g_create(rt, (made & 1u) ? IOS27_21G_COND : IOS27_21G_MUTEX,
                            &h[made]) == 0)
        ++made;
    CHECK(made == IOS27_21G_MAX_OBJECTS);
    CHECK(ios27_21g_create(rt, IOS27_21G_MUTEX, &spill) == EAGAIN);
    while (made) {
        --made;
        CHECK(ios27_21g_destroy(rt, h[made]) == 0);
    }
    CHECK(ios27_21g_runtime_destroy(rt) == 0);
}

static void test_g_create_rejects(void) {
    ios27_21g_runtime *rt;
    ios27_21g_handle m, c, h;
    if (g_open(&rt, G_CAP, &m, &c) != 0) return;
    CHECK(ios27_21g_create(NULL, IOS27_21G_MUTEX, &h) == EINVAL);
    CHECK(ios27_21g_create(rt, IOS27_21G_MUTEX, NULL) == EINVAL);
    CHECK(ios27_21g_create(rt, (enum ios27_21g_kind)0, &h) == EINVAL);
    CHECK(ios27_21g_create(rt, (enum ios27_21g_kind)3, &h) == EINVAL);
    g_expect_usable(rt, m, c, G_CAP - 2u);
    g_close(rt, m, c);
}

static void test_g_null_arguments(void) {
    const ios27_21g_handle h = {1u, 1u};
    struct timespec past = {0, 0};
    ios27_21g_stats st;
    ios27_21g_runtime *rt;
    ios27_21g_handle m, c;
    CHECK(ios27_21g_destroy(NULL, h) == EINVAL);
    CHECK(ios27_21g_mutex_lock(NULL, h) == EINVAL);
    CHECK(ios27_21g_mutex_trylock(NULL, h) == EINVAL);
    CHECK(ios27_21g_mutex_unlock(NULL, h) == EINVAL);
    CHECK(ios27_21g_cond_wait(NULL, h, h) == EINVAL);
    CHECK(ios27_21g_cond_timedwait(NULL, h, h, IOS27_21G_MONOTONIC, &past) ==
          EINVAL);
    CHECK(ios27_21g_cond_signal(NULL, h) == EINVAL);
    CHECK(ios27_21g_cond_broadcast(NULL, h) == EINVAL);
    CHECK(ios27_21g_cond_stats(NULL, h, &st) == EINVAL);
    if (g_open(&rt, G_CAP, &m, &c) != 0) return;
    CHECK(ios27_21g_cond_stats(rt, c, NULL) == EINVAL);
    g_expect_usable(rt, m, c, G_CAP - 2u);
    g_close(rt, m, c);
}

static void test_g_forged_handles(void) {
    ios27_21g_runtime *rt;
    ios27_21g_handle m, c;
    uint32_t unused = 1u;
    size_t i;
    if (g_open(&rt, G_CAP, &m, &c) != 0) return;
    while (unused == m.slot || unused == c.slot) ++unused;
    {
        const ios27_21g_handle forged[] = {
            {0u, 1u},                         /* slot 0 */
            {G_CAP + 1u, 1u},                 /* one past capacity */
            {IOS27_21G_MAX_OBJECTS + 1u, 1u}, /* one past the table */
            {UINT32_MAX, 1u},
            {m.slot, 0u},                     /* generation 0 */
            {m.slot, m.generation + 1u},      /* future generation */
            {c.slot, c.generation + 1u},
            {unused, 1u},                     /* in range, never created */
        };
        for (i = 0; i < sizeof(forged) / sizeof(forged[0]); ++i)
            g_reject_everywhere(rt, forged[i], m, c);
    }
    g_wrong_kind(rt, m, c);
    g_expect_usable(rt, m, c, G_CAP - 2u);
    g_close(rt, m, c);
}

static void test_g_stale_reuse(void) {
    ios27_21g_runtime *rt;
    ios27_21g_handle m, c, old, filler, reused;
    if (g_open(&rt, G_CAP, &m, &c) != 0) return;
    /* Fill the table so the destroyed slot is the only free one. */
    if (ios27_21g_create(rt, IOS27_21G_MUTEX, &old) != 0 ||
        ios27_21g_create(rt, IOS27_21G_COND, &filler) != 0) {
        setup_failed("21g fill to capacity");
        return;
    }
    CHECK(ios27_21g_destroy(rt, old) == 0);
    CHECK(ios27_21g_destroy(rt, old) == EINVAL);
    g_reject_everywhere(rt, old, m, c);
    if (ios27_21g_create(rt, IOS27_21G_COND, &reused) != 0) {
        setup_failed("21g reuse of the freed slot");
        return;
    }
    CHECK(reused.slot == old.slot);
    CHECK(reused.generation != old.generation);
    /* The stale mutex handle must not reach the condition now in its slot. */
    g_reject_everywhere(rt, old, m, c);
    CHECK(ios27_21g_cond_broadcast(rt, reused) == 0);
    CHECK(ios27_21g_destroy(rt, reused) == 0);
    CHECK(ios27_21g_destroy(rt, reused) == EINVAL);
    g_reject_everywhere(rt, reused, m, c);
    CHECK(ios27_21g_destroy(rt, filler) == 0);
    g_expect_usable(rt, m, c, G_CAP - 2u);
    g_close(rt, m, c);
}

static void test_g_timedwait_rejects(void) {
    static const enum ios27_21g_clock clocks[] = {IOS27_21G_REALTIME,
                                                   IOS27_21G_MONOTONIC};
    ios27_21g_runtime *rt;
    ios27_21g_handle m, c;
    struct timespec bad[2];
    size_t i, j;
    if (g_open(&rt, G_CAP, &m, &c) != 0) return;
    bad[0].tv_sec = -1;
    bad[0].tv_nsec = 0;
    bad[1].tv_sec = 0;
    bad[1].tv_nsec = -1;
    CHECK(ios27_21g_mutex_lock(rt, m) == 0);
    for (i = 0; i < 2u; ++i) {
        CHECK(ios27_21g_cond_timedwait(rt, c, m, clocks[i], NULL) == EINVAL);
        for (j = 0; j < 2u; ++j)
            CHECK(ios27_21g_cond_timedwait(rt, c, m, clocks[i], &bad[j]) ==
                  EINVAL);
    }
    /* Every reject left m owned by this thread and nothing queued on c. */
    CHECK(ios27_21g_mutex_lock(rt, m) == EDEADLK);
    CHECK(ios27_21g_mutex_unlock(rt, m) == 0);
    g_expect_usable(rt, m, c, G_CAP - 2u);
    g_close(rt, m, c);
}

int main(void) {
    test_g_runtime_bounds();
    test_g_create_rejects();
    test_g_null_arguments();
    test_g_forged_handles();
    test_g_stale_reuse();
    test_g_timedwait_rejects();
    if (failures) {
        fprintf(stderr, "libsystem21-adversarial: %d failure(s)\n", failures);
        return 1;
    }
    puts("libsystem21-adversarial: all checks passed");
    return 0;
}
