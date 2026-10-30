#include "ios27/phase26e_host.h"
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define OK(expr) assert((expr) == P26_OK)

static phase26e_host *make_host(phase26e_id *window, phase26e_id *view) {
    phase26e_host *h = phase26e_create(); assert(h);
    phase26e_id scene, root;
    OK(phase26e_scene_add(h, &scene));
    OK(phase26e_window_add(h, scene, (phase26e_rect){0,0,400,800}, window));
    OK(phase26e_view_add(h, *window, 0, (phase26e_rect){0,0,400,800}, 0, &root));
    OK(phase26e_view_add(h, *window, root, (phase26e_rect){10,20,200,200}, 2, view));
    OK(phase26e_launch(h));
    OK(phase26e_surface(h, 1, 400, 800, 0));
    return h;
}
static void base_tests(void) {
    phase26e_id w, v, child, hit;
    phase26e_host *h = make_host(&w, &v);
    OK(phase26e_hit_test(h, w, 15, 25, &hit)); assert(hit == v);
    OK(phase26e_view_add(h, w, v, (phase26e_rect){5,5,20,20}, 3, &child));
    OK(phase26e_hit_test(h, w, 16, 26, &hit)); assert(hit == child);
    phase26e_id chain[8] = {0};
    size_t nchain = phase26e_responder_chain(h, child, chain, 8);
    assert(nchain == 5 && chain[0] == child && chain[1] == v && chain[3] == w);
    float x, y;
    OK(phase26e_convert_to_window(h, child, 1, 1, &x, &y)); assert(x == 16 && y == 26);
    OK(phase26e_pointer(h, P26_DOWN, 7, 16, 26));
    OK(phase26e_pointer(h, P26_MOVE, 7, 250, 700));
    OK(phase26e_pointer(h, P26_UP, 7, 17, 27));
    phase26e_event ev[16]; size_t n = phase26e_drain(h, ev, 16);
    assert(n == 4 && ev[0].target == child && ev[1].target == child && ev[2].target == child && ev[3].kind == P26_TAP);
    OK(phase26e_pointer(h, P26_DOWN, 3, 16, 26));
    OK(phase26e_view_remove(h, v));
    assert(phase26e_get_metrics(h).cancellations == 1);
    assert(phase26e_get_metrics(h).active_touches == 0);
    assert(phase26e_accessibility_focus(h, child) == P26_NOT_FOUND);
    OK(phase26e_hit_test(h, w, 16, 26, &hit)); assert(hit != child && hit != v);
    assert(phase26e_text(h, "\xC0\xAF", 2) == P26_INVALID);
    assert(phase26e_text(h, "\xED\xA0\x80", 3) == P26_INVALID);
    assert(phase26e_text(h, "\xF4\x90\x80\x80", 4) == P26_INVALID);
    assert(phase26e_text(h, "\xE2\x82", 2) == P26_INVALID);
    OK(phase26e_text(h, "hello \xF0\x9F\x91\x8B", 10));
    OK(phase26e_compose(h, "\xC3\xA9", 2));
    assert(phase26e_text(h, "a\0b", 3) == P26_INVALID);
    OK(phase26e_keyboard(h, 1, 66));
    assert(phase26e_keyboard(h, 1, -1) == P26_INVALID);
    assert(phase26e_pointer(h, P26_DOWN, 1, NAN, 10) == P26_INVALID);
    assert(phase26e_pointer(h, P26_UP, 1, 10, 10) == P26_INVALID);
    assert(phase26e_service(h, "audio") == P26_DENIED);
    assert(phase26e_service(h, "camera") == P26_DENIED);
    uint64_t generation = phase26e_get_metrics(h).surface_generation;
    OK(phase26e_present(h, generation));
    assert(phase26e_present(h, generation) == P26_STATE);
    OK(phase26e_surface(h, 1, 800, 400, 90));
    assert(phase26e_present(h, generation) == P26_STATE);
    OK(phase26e_present(h, phase26e_get_metrics(h).surface_generation));
    assert(phase26e_surface(h, 1, 0, 400, 0) == P26_INVALID);
    assert(phase26e_surface(h, 1, 400, 400, 45) == P26_INVALID);
    for (int i = 0; i < 16; ++i) OK(phase26e_pointer(h, P26_DOWN, i, 20, 30));
    assert(phase26e_pointer(h, P26_DOWN, 16, 20, 30) == P26_FULL);
    OK(phase26e_surface(h, 0, 0, 0, 0));
    assert(phase26e_get_metrics(h).active_touches == 0);
    assert(phase26e_get_metrics(h).cancellations == 17);
    phase26e_destroy(h);
}

static void stress_tests(void) {
    phase26e_id w, v;
    phase26e_host *h = make_host(&w, &v);
    /* 100,000 input events, no dropped events in routing counters. */
    OK(phase26e_pointer(h, P26_DOWN, 9, 20, 30));
    for (unsigned i = 0; i < 100000; ++i) {
        OK(phase26e_pointer(h, P26_MOVE, 9, 20 + (float)(i % 30), 30));
        if (i % 1000 == 0) {
            phase26e_event ev[PHASE26E_MAX_EVENT_LOG];
            (void)phase26e_drain(h, ev, PHASE26E_MAX_EVENT_LOG);
        }
    }
    OK(phase26e_pointer(h, P26_UP, 9, 20, 30));
    phase26e_metrics m = phase26e_get_metrics(h);
    assert(m.accepted_events >= 100002 && m.active_touches == 0);
    /* 10,000 lifecycle transitions while a surface is recreated repeatedly. */
    for (unsigned i = 0; i < 5000; ++i) {
        OK(phase26e_pause(h));
        assert(phase26e_pointer(h, P26_DOWN, 1, 10, 10) == P26_STATE);
        OK(phase26e_surface(h, 0, 0, 0, 0));
        OK(phase26e_surface(h, 1, 400, 800, (i & 1) ? 0u : 180u));
        OK(phase26e_resume(h));
    }
    m = phase26e_get_metrics(h);
    assert(m.lifecycle_transitions == 10001 && m.surface_recreations == 10001);
    assert(m.rejected_events >= 5000 && m.active_touches == 0);
    OK(phase26e_terminate(h));
    assert(phase26e_pointer(h, P26_DOWN, 1, 10, 10) == P26_STATE);
    assert(phase26e_resume(h) == P26_STATE);
    phase26e_destroy(h);
}

struct work { phase26e_host *host; atomic_int *go; };
static void *worker(void *user) {
    struct work *w = (struct work *)user;
    while (!atomic_load(w->go)) { /* no sleeping or open-ended polling: test controller always sets */ }
    for (int i = 0; i < 20000; ++i)
        (void)phase26e_keyboard(w->host, (i & 1), 66);
    return NULL;
}
static void concurrent_tests(void) {
    phase26e_id window, view;
    phase26e_host *h = make_host(&window, &view);
    pthread_t threads[4]; atomic_int go = 0;
    struct work w = {h, &go};
    for (unsigned i = 0; i < 4; ++i) assert(pthread_create(&threads[i], NULL, worker, &w) == 0);
    atomic_store(&go, 1);
    OK(phase26e_terminate(h));
    for (unsigned i = 0; i < 4; ++i) assert(pthread_join(threads[i], NULL) == 0);
    assert(phase26e_get_metrics(h).lifecycle == P26_TERMINATED);
    assert(phase26e_get_metrics(h).active_touches == 0);
    phase26e_destroy(h);
}
int main(void) {
    base_tests();
    stress_tests();
    concurrent_tests();
    puts("phase26e: scene, hit test, input (100k), lifecycle (10k), termination concurrency passed");
    return 0;
}
