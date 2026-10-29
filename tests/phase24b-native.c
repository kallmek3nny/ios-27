#define _POSIX_C_SOURCE 200809L
#include "ios27/phase24b_lifecycle.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "phase24b assert %s at %s:%d\n", #c, __FILE__, __LINE__); \
    abort(); } } while (0)

typedef struct {
    unsigned init[IOS27_24B_IMAGES], term[IOS27_24B_IMAGES];
    unsigned compensate, reentrant, invoke_count, failed_once;
    unsigned start_order[IOS27_24B_IMAGES], stop_order[IOS27_24B_IMAGES];
    unsigned start_n, stop_n, fail_image, fail_ordinal, fail_term;
    ios27_24b_runtime *runtime;
} model;

static int callback(void *p, unsigned id, ios27_24b_action a,
                    uint64_t pc, size_t ordinal) {
    model *m = p;
    CHECK(id < IOS27_24B_IMAGES);
    CHECK(pc >= 0x1000u && (pc & 3u) == 0);
    ++m->invoke_count;
    if (m->reentrant && a == IOS27_24B_INIT) {
        ios27_24b_handle dummy;
        CHECK(ios27_24b_open(m->runtime, id, &dummy) == EDEADLK);
        m->reentrant = 0;
    }
    if (a == IOS27_24B_COMPENSATE) {
        ++m->compensate;
        return 0; /* explicitly reversible model state only */
    }
    if (a == IOS27_24B_INIT) {
        if (m->fail_image == id + 1u &&
            m->fail_ordinal == ordinal && !m->failed_once) {
            m->failed_once = 1;
            return EIO;
        }
        if (m->init[id]++ == 0 && m->start_n < IOS27_24B_IMAGES)
            m->start_order[m->start_n++] = id;
    } else {
        if (m->fail_term == id + 1u) return EIO;
        if (m->term[id]++ == 0 && m->stop_n < IOS27_24B_IMAGES)
            m->stop_order[m->stop_n++] = id;
    }
    return 0;
}
static void fake_images(ios27_24b_image *images) {
    memset(images, 0, sizeof(*images) * 6u);
    for (unsigned i = 0; i < 6; ++i) {
        images[i].parsed = images[i].bound = 1;
        images[i].executable_count = 1;
        images[i].executable[0] = (ios27_24b_range){
            0x1000u + i * 0x100u, 0x1080u + i * 0x100u};
        images[i].init_count = images[i].term_count = 1;
        images[i].init[0] = 0x1000u + i * 0x100u;
        images[i].term[0] = 0x1004u + i * 0x100u;
    }
}
static void populate(ios27_24b_runtime *r, ios27_24b_image *im) {
    const unsigned dep1[] = {0}, dep2[] = {0}, dep3[] = {1, 2};
    const unsigned dep4[] = {2, 3}, dep5[] = {1, 4};
    CHECK(ios27_24b_register(r, 0, im, NULL, 0) == 0);
    CHECK(ios27_24b_register(r, 1, im + 1, dep1, 1) == 0);
    CHECK(ios27_24b_register(r, 2, im + 2, dep2, 1) == 0);
    CHECK(ios27_24b_register(r, 3, im + 3, dep3, 2) == 0);
    CHECK(ios27_24b_register(r, 4, im + 4, dep4, 2) == 0);
    CHECK(ios27_24b_register(r, 5, im + 5, dep5, 2) == 0);
}
static void reset_log(model *m) {
    memset(m, 0, sizeof(*m));
}
static void check_order(model *m) {
    CHECK(m->start_n == 6 && m->stop_n == 6);
    for (unsigned i = 0; i < 6; ++i) {
        CHECK(m->start_order[i] == i);
        CHECK(m->stop_order[i] == 5 - i);
    }
}
static void stress_graph(void) {
    ios27_24b_runtime r;
    ios27_24b_image im[6];
    ios27_24b_handle h, a;
    ios27_24b_snapshot snapshot;
    model m;
    fake_images(im);
    CHECK(ios27_24b_runtime_init(&r, IOS27_24B_MODELED) == 0);
    populate(&r, im);
    reset_log(&m);
    m.runtime = &r;
    for (unsigned round = 0; round < 10000; ++round) {
        reset_log(&m); m.runtime = &r;
        CHECK(ios27_24b_open(&r, 5, &h) == 0);
        CHECK(ios27_24b_ensure(&r, h, callback, callback, &m) == 0);
        CHECK(ios27_24b_ensure(&r, h, callback, callback, &m) == 0);
        CHECK(ios27_24b_close(&r, h, callback, &m) == 0);
        check_order(&m);
        CHECK(ios27_24b_close(&r, h, callback, &m) == ESTALE);
        if (round == 0) {
            CHECK(ios27_24b_inspect(&r, 0, &snapshot) == 0);
            CHECK(snapshot.state == IOS27_24B_NEW &&
                  snapshot.references == 0);
        }
    }
    CHECK(r.startups == 60000 && r.shutdowns == 60000);
    CHECK(r.callbacks >= 120000 && r.transitions >= 20000);
    /* Every handle pins each transitive dependency exactly once. */
    reset_log(&m); m.runtime = &r;
    CHECK(ios27_24b_open(&r, 5, &h) == 0);
    CHECK(ios27_24b_open(&r, 4, &a) == 0);
    CHECK(ios27_24b_ensure(&r, h, callback, callback, &m) == 0);
    CHECK(ios27_24b_close(&r, h, callback, &m) == 0);
    CHECK(m.term[5] == 1 && m.term[4] == 0);
    CHECK(ios27_24b_ensure(&r, a, callback, callback, &m) == 0);
    CHECK(ios27_24b_close(&r, a, callback, &m) == 0);
    CHECK(ios27_24b_runtime_destroy(&r) == 0);
    puts("phase24b: 10000 overlapping graph lifecycles, 120000+ modeled callbacks");
}
typedef struct {
    ios27_24b_runtime *r;
    ios27_24b_handle h;
    model *m;
    int result;
} worker;
static void *worker_main(void *arg) {
    worker *w = arg;
    w->result = ios27_24b_ensure(w->r, w->h, callback, callback, w->m);
    return NULL;
}
static void concurrency(void) {
    ios27_24b_runtime r;
    ios27_24b_image im[6];
    pthread_t threads[8];
    worker workers[8];
    model m;
    fake_images(im);
    CHECK(ios27_24b_runtime_init(&r, IOS27_24B_MODELED) == 0);
    populate(&r, im);
    reset_log(&m); m.runtime = &r;
    for (size_t i = 0; i < 8; ++i) {
        workers[i].r = &r; workers[i].m = &m;
        workers[i].result = -1;
        CHECK(ios27_24b_open(&r, 5, &workers[i].h) == 0);
    }
    for (size_t i = 0; i < 8; ++i)
        CHECK(pthread_create(&threads[i], NULL, worker_main, &workers[i]) == 0);
    for (size_t i = 0; i < 8; ++i) {
        CHECK(pthread_join(threads[i], NULL) == 0);
        CHECK(workers[i].result == 0);
    }
    for (unsigned i = 0; i < 6; ++i) CHECK(m.init[i] == 1);
    for (size_t i = 0; i < 8; ++i)
        CHECK(ios27_24b_close(&r, workers[i].h, callback, &m) == 0);
    for (unsigned i = 0; i < 6; ++i) CHECK(m.term[i] == 1);
    CHECK(ios27_24b_runtime_destroy(&r) == 0);
    puts("phase24b: concurrent init, unique handles, shared graph exactly once");
}
static void failure_cases(void) {
    ios27_24b_runtime r;
    ios27_24b_image im[6];
    ios27_24b_handle h;
    ios27_24b_snapshot snap;
    model m;
    fake_images(im);
    im[2].init_count = 2;
    im[2].init[1] = im[2].init[0] + 8;
    CHECK(ios27_24b_runtime_init(&r, IOS27_24B_MODELED) == 0);
    populate(&r, im);
    reset_log(&m); m.runtime = &r;
    m.fail_image = 3; m.fail_ordinal = 1;
    CHECK(ios27_24b_open(&r, 5, &h) == 0);
    CHECK(ios27_24b_ensure(&r, h, callback, callback, &m) == EIO);
    CHECK(m.compensate == 1);
    CHECK(ios27_24b_inspect(&r, 2, &snap) == 0);
    CHECK(snap.state == IOS27_24B_FAILED && !snap.poisoned);
    CHECK(ios27_24b_ensure(&r, h, callback, callback, &m) == EIO);
    CHECK(ios27_24b_close(&r, h, callback, &m) == 0);
    CHECK(ios27_24b_open(&r, 5, &h) == 0);
    m.fail_image = 0;
    CHECK(ios27_24b_ensure(&r, h, callback, callback, &m) == 0);
    CHECK(ios27_24b_close(&r, h, callback, &m) == 0);
    CHECK(ios27_24b_runtime_destroy(&r) == 0);
    /* Recursion is rejected, never allowed to deadlock the callback thread. */
    fake_images(im);
    CHECK(ios27_24b_runtime_init(&r, IOS27_24B_MODELED) == 0);
    populate(&r, im);
    reset_log(&m); m.runtime = &r; m.reentrant = 1;
    CHECK(ios27_24b_open(&r, 5, &h) == 0);
    CHECK(ios27_24b_ensure(&r, h, callback, callback, &m) == 0);
    CHECK(ios27_24b_close(&r, h, callback, &m) == 0);
    CHECK(ios27_24b_runtime_destroy(&r) == 0);
    /* Uncompensated partial startup must never permit unload. */
    fake_images(im);
    im[2].init_count = 2;
    im[2].init[1] = im[2].init[0] + 8;
    CHECK(ios27_24b_runtime_init(&r, IOS27_24B_MODELED) == 0);
    populate(&r, im);
    reset_log(&m); m.runtime = &r;
    m.fail_image = 3; m.fail_ordinal = 1;
    CHECK(ios27_24b_open(&r, 5, &h) == 0);
    CHECK(ios27_24b_ensure(&r, h, callback, NULL, &m) == EIO);
    CHECK(ios27_24b_inspect(&r, 2, &snap) == 0);
    CHECK(snap.state == IOS27_24B_QUARANTINED && snap.poisoned);
    CHECK(ios27_24b_close(&r, h, callback, &m) == ENOTRECOVERABLE);
    CHECK(ios27_24b_runtime_destroy(&r) == EBUSY);
    puts("phase24b: sticky failure, explicit compensation, reentry, quarantine");
}
typedef struct {
    uint64_t base, pointers[2], offsets[2];
    int fail;
} mapped;
static int mapped_read(void *context, uint64_t addr,
                       unsigned width, uint64_t *out) {
    mapped *m = context;
    if (m->fail) return EIO;
    if (addr == m->base && width == 8) { *out = m->pointers[0]; return 0; }
    if (addr == m->base + 8 && width == 8) {
        *out = m->pointers[1]; return 0;
    }
    if (addr == m->base + 16 && width == 4) {
        *out = m->offsets[0]; return 0;
    }
    return ENOENT;
}
static void relocation(void) {
    ios27_24b_image im = {0}, old;
    mapped m = {0};
    im.parsed = 1; im.executable_count = 1;
    im.executable[0] = (ios27_24b_range){0x1000, 0x1100};
    im.text_vmaddr = 0x1000;
    im.init_table = 0x2000;
    im.term_table = 0x2008;
    im.offset_table = 0x2010;
    im.init_count = im.term_count = im.offset_count = 1;
    m.base = 0x3000; m.pointers[0] = 0x2004; m.pointers[1] = 0x2008;
    m.offsets[0] = 0xcu;
    CHECK(ios27_24b_bind(&im, 0x1000, mapped_read, &m) == 0);
    CHECK(im.bound && im.init_count == 2 && im.init[0] == 0x2004);
    CHECK(im.init[1] == 0x200c && im.term[0] == 0x2008);
    old = im;
    CHECK(ios27_24b_bind(&im, 0, mapped_read, &m) == EINVAL);
    CHECK(memcmp(&old, &im, sizeof(im)) == 0);
    memset(&im, 0, sizeof(im));
    im.parsed = 1; im.executable_count = 1;
    im.executable[0] = (ios27_24b_range){0x1000, 0x1100};
    im.init_table = 0x2000; im.init_count = 1;
    old = im;
    m.pointers[0] = 0xdeadbeefu;
    CHECK(ios27_24b_bind(&im, 0x1000, mapped_read, &m) == ENOEXEC);
    CHECK(memcmp(&old, &im, sizeof(im)) == 0);
    puts("phase24b: relocation-aware transactional bind and invalid target");
}
static void linked_files(int argc, char **argv) {
    CHECK(argc == 7);
    for (int i = 1; i < argc; ++i) {
        FILE *f = fopen(argv[i], "rb");
        CHECK(f != NULL);
        CHECK(fseek(f, 0, SEEK_END) == 0);
        long n = ftell(f);
        CHECK(n >= 32 && n <= 4 * 1024 * 1024);
        CHECK(fseek(f, 0, SEEK_SET) == 0);
        uint8_t *bytes = malloc((size_t)n);
        CHECK(bytes != NULL);
        CHECK(fread(bytes, 1, (size_t)n, f) == (size_t)n);
        CHECK(fclose(f) == 0);
        ios27_24b_image image, untouched = {0};
        CHECK(ios27_24b_parse(bytes, (size_t)n, &image) == 0);
        CHECK(image.parsed && image.init_count >= 1 &&
              image.term_count >= 1 && image.executable_count >= 1);
        bytes[0] = 0;
        image = untouched;
        CHECK(ios27_24b_parse(bytes, (size_t)n, &image) == ENOEXEC);
        CHECK(memcmp(&image, &untouched, sizeof(image)) == 0);
        free(bytes);
    }
    puts("phase24b: 6 genuine LLVM-linked Darwin dylibs parsed (not executed)");
}
int main(int argc, char **argv) {
    linked_files(argc, argv);
    relocation();
    stress_graph();
    concurrency();
    failure_cases();
    puts("phase24b: ALL NATIVE HOST TESTS PASS; DARWIN GUEST CALLBACKS EXECUTED: 0");
    return 0;
}
