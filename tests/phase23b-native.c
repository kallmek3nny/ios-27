#define _POSIX_C_SOURCE 200809L
#include "ios27/phase23b_lifecycle.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cv;
    unsigned trace[32];
    unsigned trace_count;
    atomic_uint starts[6], stops[6], undos;
    int record, fail_once, recurse, recursive_result, gate, entered, released;
    ios27_23b_runtime *runtime;
    ios27_23b_handle handle;
} harness;
static void h_init(harness *h) {
    memset(h, 0, sizeof(*h));
    assert(!pthread_mutex_init(&h->lock, NULL));
    assert(!pthread_cond_init(&h->cv, NULL));
}
static void h_destroy(harness *h) {
    assert(!pthread_cond_destroy(&h->cv));
    assert(!pthread_mutex_destroy(&h->lock));
}
static int dispatch(void *p, unsigned id, ios27_23b_action action,
                    uint64_t target, size_t ordinal) {
    harness *h = p;
    assert(id < 6 && target && !(target & 3u));
    if (action == IOS27_23B_ROLLBACK) {
        atomic_fetch_add(&h->undos, 1);
        return 0;
    }
    if (action == IOS27_23B_CONSTRUCT) {
        if (h->recurse) {
            h->recurse = 0;
            h->recursive_result = ios27_23b_ensure(h->runtime, h->handle,
                                                     dispatch, h);
        }
        if (h->gate && id == 0) {
            assert(!pthread_mutex_lock(&h->lock));
            h->entered = 1;
            assert(!pthread_cond_broadcast(&h->cv));
            while (!h->released) assert(!pthread_cond_wait(&h->cv, &h->lock));
            assert(!pthread_mutex_unlock(&h->lock));
        }
        if (h->fail_once && id == 0 && ordinal == 1) {
            h->fail_once = 0;
            return EIO;
        }
        atomic_fetch_add(&h->starts[id], 1);
    } else {
        atomic_fetch_add(&h->stops[id], 1);
    }
    if (h->record) {
        assert(!pthread_mutex_lock(&h->lock));
        if (h->trace_count < 32)
            h->trace[h->trace_count++] = id + (action == IOS27_23B_DESTRUCT ? 16u : 0u);
        assert(!pthread_mutex_unlock(&h->lock));
    }
    return 0;
}
static unsigned char *readfile(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    assert(f);
    assert(!fseek(f, 0, SEEK_END));
    long size = ftell(f);
    assert(size > 32 && size <= 4 * 1024 * 1024);
    assert(!fseek(f, 0, SEEK_SET));
    unsigned char *b = malloc((size_t)size);
    assert(b);
    assert(fread(b, 1, (size_t)size, f) == (size_t)size);
    assert(!fclose(f));
    *n = (size_t)size;
    return b;
}
typedef struct { uint64_t callback; unsigned reads; } resolved;
static int reader(void *context, uint64_t address, uint64_t *out) {
    resolved *r = context;
    if (!address || (address & 7u)) return EINVAL;
    ++r->reads;
    *out = r->callback;
    return 0;
}
static void preflight_and_load(ios27_23b_runtime *r, const char *const *paths) {
    for (unsigned i = 0; i < 6; ++i) {
        size_t n;
        unsigned char *file = readfile(paths[i], &n);
        ios27_23b_image parsed, snapshot;
        assert(!ios27_23b_parse(file, n, &parsed));
        assert(parsed.init_count == 1 && parsed.term_count == 1);
        resolved mapping = {parsed.exec[0].start + 8, 0};
        assert(!ios27_23b_bind(&parsed, reader, &mapping));
        assert(mapping.reads == 2);
        snapshot = parsed;
        mapping.callback = 0;
        assert(ios27_23b_bind(&parsed, reader, &mapping) == ENOEXEC);
        assert(!memcmp(&parsed, &snapshot, sizeof(parsed)));
        mapping.callback = parsed.exec[0].end + 8;
        assert(ios27_23b_bind(&parsed, reader, &mapping) == ENOEXEC);
        /* Mutate the genuine image header and section records, never execute. */
        file[0] ^= 0xff;
        assert(ios27_23b_parse(file, n, &parsed) == ENOEXEC);
        file[0] ^= 0xff;
        file[20] = 0xff;
        assert(ios27_23b_parse(file, n, &parsed) == ENOEXEC);
        free(file);
        unsigned dep = i - 1;
        assert(!ios27_23b_add(r, i, &snapshot, i ? &dep : NULL, i ? 1u : 0u));
    }
}
typedef struct { ios27_23b_runtime *rt; ios27_23b_handle h; harness *ctx; int status; } worker;
static void *run_ensure(void *p) {
    worker *w = p;
    w->status = ios27_23b_ensure(w->rt, w->h, dispatch, w->ctx);
    return NULL;
}
static void order_concurrency_and_stress(ios27_23b_runtime *r, harness *h) {
    ios27_23b_handle handle;
    assert(!ios27_23b_open(r, 5, &handle));
    h->runtime = r; h->handle = handle; h->record = 1;
    pthread_t tids[8]; worker work[8];
    for (int j = 0; j < 8; ++j) {
        work[j] = (worker){r, handle, h, -1};
        assert(!pthread_create(&tids[j], NULL, run_ensure, &work[j]));
    }
    for (int j = 0; j < 8; ++j) {
        assert(!pthread_join(tids[j], NULL));
        assert(!work[j].status);
    }
    for (unsigned i = 0; i < 6; ++i) assert(atomic_load(&h->starts[i]) == 1);
    assert(!ios27_23b_close(r, handle, dispatch, h));
    for (unsigned i = 0; i < 6; ++i) {
        assert(h->trace[i] == i);
        assert(h->trace[i + 6] == 16 + 5 - i);
        assert(atomic_load(&h->stops[i]) == 1);
    }
    assert(h->trace_count == 12);
    assert(ios27_23b_ensure(r, handle, dispatch, h) == ESTALE);
    assert(ios27_23b_close(r, handle, dispatch, h) == ESTALE);
    h->record = 0;
    for (unsigned round = 0; round < 10000; ++round) {
        ios27_23b_handle a, b;
        assert(!ios27_23b_open(r, 5, &a));
        assert(!ios27_23b_open(r, 5, &b));
        assert(a.ticket != b.ticket);
        assert(!ios27_23b_ensure(r, a, dispatch, h));
        assert(!ios27_23b_ensure(r, b, dispatch, h));
        assert(!ios27_23b_close(r, a, dispatch, h));
        assert(ios27_23b_close(r, a, dispatch, h) == ESTALE);
        assert(!ios27_23b_close(r, b, dispatch, h));
    }
    for (unsigned i = 0; i < 6; ++i) {
        ios27_23b_state state; unsigned refs, active;
        assert(atomic_load(&h->starts[i]) == 10001);
        assert(atomic_load(&h->stops[i]) == 10001);
        assert(!ios27_23b_snapshot(r, i, &state, &refs, &active));
        assert(state == IOS27_23B_NEW && !refs && !active);
    }
}
static void fail_and_recover(void) {
    ios27_23b_runtime r;
    ios27_23b_image img = {0};
    harness h;
    h_init(&h);
    assert(!ios27_23b_runtime_init(&r));
    img.exec_count = 1;
    img.exec[0] = (ios27_23b_exec_range){0x1000, 0x2000};
    img.init_count = 2;
    img.init[0] = 0x1000;
    img.init[1] = 0x1004;
    img.term_count = 1;
    img.term[0] = 0x1008;
    assert(!ios27_23b_add(&r, 0, &img, NULL, 0));
    ios27_23b_handle t;
    assert(!ios27_23b_open(&r, 0, &t));
    h.fail_once = 1;
    assert(ios27_23b_ensure(&r, t, dispatch, &h) == EIO);
    assert(atomic_load(&h.undos) == 1);
    assert(ios27_23b_ensure(&r, t, dispatch, &h) == EIO);
    assert(!ios27_23b_close(&r, t, dispatch, &h));
    assert(!atomic_load(&h.stops[0]));
    assert(!ios27_23b_open(&r, 0, &t));
    h.runtime = &r; h.handle = t; h.recurse = 1;
    assert(!ios27_23b_ensure(&r, t, dispatch, &h));
    assert(h.recursive_result == EDEADLK);
    assert(!ios27_23b_close(&r, t, dispatch, &h));
    assert(!ios27_23b_runtime_destroy(&r));
    h_destroy(&h);
}
static void active_callback_guard(void) {
    ios27_23b_runtime r;
    ios27_23b_image img = {0}; harness h; worker w; pthread_t tid;
    h_init(&h);
    assert(!ios27_23b_runtime_init(&r));
    img.exec_count = 1;
    img.exec[0] = (ios27_23b_exec_range){0x1000, 0x2000};
    img.init_count = 1; img.init[0] = 0x1000;
    img.term_count = 1; img.term[0] = 0x1004;
    assert(!ios27_23b_add(&r, 0, &img, NULL, 0));
    ios27_23b_handle t;
    assert(!ios27_23b_open(&r, 0, &t));
    h.gate = 1;
    w = (worker){&r, t, &h, -1};
    assert(!pthread_create(&tid, NULL, run_ensure, &w));
    assert(!pthread_mutex_lock(&h.lock));
    while (!h.entered) assert(!pthread_cond_wait(&h.cv, &h.lock));
    assert(!pthread_mutex_unlock(&h.lock));
    unsigned active;
    assert(!ios27_23b_snapshot(&r, 0, NULL, NULL, &active) && active == 1);
    assert(ios27_23b_close(&r, t, dispatch, &h) == EBUSY);
    assert(ios27_23b_runtime_destroy(&r) == EBUSY);
    assert(!pthread_mutex_lock(&h.lock));
    h.released = 1;
    assert(!pthread_cond_broadcast(&h.cv));
    assert(!pthread_mutex_unlock(&h.lock));
    assert(!pthread_join(tid, NULL) && !w.status);
    assert(!ios27_23b_close(&r, t, dispatch, &h));
    assert(!ios27_23b_runtime_destroy(&r));
    h_destroy(&h);
}
static void cycles(void) {
    ios27_23b_runtime r;
    ios27_23b_image img = {0};
    unsigned one = 1, zero = 0;
    assert(!ios27_23b_runtime_init(&r));
    assert(!ios27_23b_add(&r, 0, &img, &one, 1));
    assert(!ios27_23b_add(&r, 1, &img, &zero, 1));
    ios27_23b_handle h;
    assert(ios27_23b_open(&r, 0, &h) == ELOOP);
    assert(!ios27_23b_runtime_destroy(&r));
}
int main(int argc, char **argv) {
    assert(argc == 7);
    ios27_23b_runtime r;
    harness h;
    h_init(&h);
    assert(!ios27_23b_runtime_init(&r));
    preflight_and_load(&r, (const char *const *)(argv + 1));
    order_concurrency_and_stress(&r, &h);
    assert(!ios27_23b_runtime_destroy(&r));
    h_destroy(&h);
    fail_and_recover();
    active_callback_guard();
    cycles();
    puts("phase23b: six real arm64 dylibs parsed; 120012 callback transitions + 120000 reference transitions, ordering, concurrency, rollback, recursion, and unload guards passed");
    return 0;
}
