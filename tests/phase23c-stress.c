#include "ios27/phase23c_bridge.h"
#include "ios27/objc_metadata.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { \
    (void)fprintf(stderr, "phase23c fail %s:%d: %s\n", __FILE__, __LINE__, #x); \
    exit(1); } } while (0)
#define THREADS 8u
#define ITERATIONS 16000u
static atomic_uint init_calls, failures;
static uintptr_t root_value(void *r, phase22d_sel s) {
    (void)r; (void)s; return 11u;
}
static uintptr_t root_add(void *r, phase22d_sel s, uintptr_t n) {
    (void)r; (void)s; return 11u + n;
}
static uintptr_t other_value(void *r, phase22d_sel s) {
    (void)r; (void)s; return 42u;
}
static uintptr_t overlay_value(void *r, phase22d_sel s) {
    (void)r; (void)s; return 77u;
}
static int init_root(void *ctx, void *cls) {
    (void)ctx; (void)cls;
    atomic_fetch_add(&init_calls, 1u);
    return 0;
}
typedef struct {
    phase23c_bridge *bridge;
    void *object;
    phase22d_sel value, add;
} worker_arg;
static void *worker(void *p) {
    worker_arg *w = p;
    for (unsigned i = 0; i < ITERATIONS; ++i) {
        uintptr_t result = 0, arg = i % 5u;
        if (phase23c_retain(w->bridge, w->object) ||
            phase23c_send_words(w->bridge, w->object, w->value,
                                NULL, NULL, 0, &result) ||
            (result != 11u && result != 77u) ||
            phase23c_send_words(w->bridge, w->object, w->add,
                                NULL, &arg, 1, &result) ||
            result != 11u + arg ||
            phase23c_release(w->bridge, w->object))
            atomic_fetch_add(&failures, 1u);
    }
    return NULL;
}
typedef struct {
    phase23c_bridge *bridge;
    const char *name;
    const uint8_t *bytes;
    size_t length;
    const phase22d_class_spec *classes;
    size_t count;
    phase23c_image *image;
    char error[256];
    int result;
} import_arg;
static void *import_worker(void *p) {
    import_arg *a = p;
    a->result = phase23c_import(a->bridge, a->name, a->bytes, a->length,
                                a->classes, a->count, a->image,
                                a->error, sizeof(a->error));
    return NULL;
}
static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    CHECK(fseek(f, 0, SEEK_END) == 0);
    long n = ftell(f);
    CHECK(n > 0 && n < 2L * 1024L * 1024L);
    CHECK(fseek(f, 0, SEEK_SET) == 0);
    uint8_t *bytes = malloc((size_t)n);
    CHECK(bytes != NULL);
    CHECK(fread(bytes, 1u, (size_t)n, f) == (size_t)n);
    CHECK(fclose(f) == 0);
    *size = (size_t)n;
    return bytes;
}
int main(int argc, char **argv) {
    CHECK(argc == 3);
    size_t nroot, nother;
    uint8_t *root = read_file(argv[1], &nroot);
    uint8_t *other = read_file(argv[2], &nother);
    phase23c_bridge *b = NULL;
    CHECK(phase23c_create(&b) == 0);
    const phase22d_method_spec rmethods[] = {
        {"value", PHASE22D_WORD0, {.word0 = root_value}},
        {"add:", PHASE22D_WORD1, {.word1 = root_add}}
    };
    const phase22d_method_spec omethods[] = {
        {"other", PHASE22D_WORD0, {.word0 = other_value}}
    };
    const phase22d_method_spec child_methods[] = {
        {"childValue", PHASE22D_WORD0, {.word0 = other_value}}
    };
    const phase22d_method_spec other_child_methods[] = {
        {"otherExtra", PHASE22D_WORD0, {.word0 = other_value}}
    };
    phase22d_class_spec rclasses[] = {
        {.name = "Phase23cRoot", .instance_size = 8u,
         .instance_methods = rmethods, .instance_count = 2u,
         .initialize = init_root},
        {.name = "Phase23cChild", .superclass = "Phase23cRoot",
         .instance_size = 8u,
         .instance_methods = child_methods, .instance_count = 1u}
    };
    phase22d_class_spec oclasses[] = {
        {.name = "Phase23cOther", .instance_size = 8u,
         .instance_methods = omethods, .instance_count = 1u},
        {.name = "Phase23cOtherChild", .superclass = "Phase23cOther",
         .instance_size = 8u,
         .instance_methods = other_child_methods, .instance_count = 1u}
    };
    phase23c_image ir = {0}, io = {0}, broken = {0};
    char err[256] = {0};
    ios27_objc_metadata *metadata = calloc(1u, sizeof(*metadata));
    CHECK(metadata != NULL);
    CHECK(ios27_objc_metadata_inspect(root, nroot, metadata,
                                      err, sizeof(err)) == 0);
    CHECK(metadata->class_count == 2u);
    for (size_t i = 0; i < metadata->class_count; ++i) {
        if (!strcmp(metadata->classes[i].name, "Phase23cRoot"))
            rclasses[0].instance_size = metadata->classes[i].instance_size;
        if (!strcmp(metadata->classes[i].name, "Phase23cChild"))
            rclasses[1].instance_size = metadata->classes[i].instance_size;
    }
    CHECK(rclasses[0].instance_size >= 8u && rclasses[1].instance_size >= 8u);
    memset(metadata, 0, sizeof(*metadata));
    CHECK(ios27_objc_metadata_inspect(other, nother, metadata,
                                      err, sizeof(err)) == 0);
    CHECK(metadata->class_count == 2u);
    for (size_t i = 0; i < metadata->class_count; ++i) {
        if (!strcmp(metadata->classes[i].name, "Phase23cOther"))
            oclasses[0].instance_size = metadata->classes[i].instance_size;
        if (!strcmp(metadata->classes[i].name, "Phase23cOtherChild"))
            oclasses[1].instance_size = metadata->classes[i].instance_size;
    }
    CHECK(oclasses[0].instance_size >= 8u && oclasses[1].instance_size >= 8u);
    free(metadata);
    import_arg imports[2] = {
        {.bridge = b, .name = "root.dylib", .bytes = root,
         .length = nroot, .classes = rclasses, .count = 2u, .image = &ir,
         .result = -1},
        {.bridge = b, .name = "other.dylib", .bytes = other,
         .length = nother, .classes = oclasses, .count = 2u, .image = &io,
         .result = -1}
    };
    pthread_t registration[2];
    for (size_t i = 0; i < 2u; ++i)
        CHECK(pthread_create(&registration[i], NULL, import_worker,
                             &imports[i]) == 0);
    for (size_t i = 0; i < 2u; ++i) {
        CHECK(pthread_join(registration[i], NULL) == 0);
        if (imports[i].result)
            (void)fprintf(stderr, "phase23c concurrent import: %s\n",
                          imports[i].error);
        CHECK(imports[i].result == 0);
    }
    CHECK(phase23c_import(b, "root.dylib", root, nroot, rclasses, 2u,
                          &broken, err, sizeof(err)) != 0);
    CHECK(broken.generation == 0);
    uint8_t *corrupt = malloc(nroot);
    CHECK(corrupt != NULL);
    memcpy(corrupt, root, nroot);
    corrupt[0] ^= 0x80u;
    CHECK(phase23c_import(b, "corrupt.dylib", corrupt, nroot, rclasses, 2u,
                          &broken, err, sizeof(err)) != 0);
    free(corrupt);
    phase22d_sel sel = NULL, add = NULL, other_sel = NULL;
    CHECK(phase23c_selector(b, "value", &sel) == 0);
    CHECK(phase23c_selector(b, "add:", &add) == 0);
    CHECK(phase23c_selector(b, "other", &other_sel) == 0);
    void *obj = NULL, *obj2 = NULL, *not_owned = NULL;
    CHECK(phase23c_allocate(b, ir, "Phase23cRoot", &obj) == 0);
    CHECK(phase23c_allocate(b, io, "Phase23cOther", &obj2) == 0);
    CHECK(phase23c_allocate(b, ir, "Phase23cOther", &not_owned) != 0);
    CHECK(not_owned == NULL);
    uintptr_t value = 0;
    CHECK(phase23c_send_words(b, obj2, other_sel, NULL, NULL, 0, &value) == 0);
    CHECK(value == 42u);
    void *weak = NULL;
    CHECK(phase23c_weak_store(b, &weak, obj) == 0);
    CHECK(weak == obj);
    pthread_t threads[THREADS];
    worker_arg arg = {b, obj, sel, add};
    for (unsigned i = 0; i < THREADS; ++i)
        CHECK(pthread_create(&threads[i], NULL, worker, &arg) == 0);
    for (unsigned i = 0; i < THREADS; ++i)
        CHECK(pthread_join(threads[i], NULL) == 0);
    CHECK(atomic_load(&failures) == 0u);
    const phase22d_method_spec override[] = {
        {"value", PHASE22D_WORD0, {.word0 = overlay_value}}
    };
    const phase22d_category category = {
        .class_name = "Phase23cRoot", .methods = override, .count = 1u
    };
    CHECK(phase23c_category(b, io, &category) == 0);
    for (unsigned i = 0; i < THREADS; ++i)
        CHECK(pthread_create(&threads[i], NULL, worker, &arg) == 0);
    for (unsigned i = 0; i < THREADS; ++i)
        CHECK(pthread_join(threads[i], NULL) == 0);
    CHECK(atomic_load(&failures) == 0u);
    CHECK(atomic_load(&init_calls) == 1u);
    CHECK(phase23c_send_words(b, obj, sel, NULL, NULL, 0, &value) == 0);
    CHECK(value == 77u);
    CHECK(phase23c_send_words(b, obj, add, NULL, NULL, 0, &value) != 0);
    CHECK(phase23c_unload(b, ir) == -2);
    CHECK(phase23c_destroy(b) != 0);
    CHECK(phase23c_release(b, obj) == 0);
    CHECK(weak == NULL);
    void *locked = NULL;
    CHECK(phase23c_weak_load_retained(b, &weak, &locked) == 0);
    CHECK(locked == NULL);
    CHECK(phase23c_weak_unregister(b, &weak) == 0);
    CHECK(phase23c_release(b, obj2) == 0);
    CHECK(phase23c_retain(b, obj) != 0);
    CHECK(phase23c_validate(b) == 0);
    phase23c_stats stats;
    CHECK(phase23c_statistics(b, &stats) == 0);
    CHECK(stats.substrate.sends >= 2u * THREADS * ITERATIONS);
    CHECK(stats.live_objects == 0 && stats.active_sends == 0);
    CHECK(stats.category_replacements == 1u);
    (void)printf("phase23c bridge ok sends=%llu retains=%llu releases=%llu imports=%llu\n",
                 (unsigned long long)stats.sends,
                 (unsigned long long)stats.retains,
                 (unsigned long long)stats.releases,
                 (unsigned long long)stats.image_imports);
    CHECK(phase23c_destroy(b) == 0);
    for (unsigned cycle = 0; cycle < 64u; ++cycle) {
        phase23c_bridge *replay = NULL;
        phase23c_image image = {0};
        void *instance = NULL;
        phase22d_sel method = NULL;
        uintptr_t result = 0;
        CHECK(phase23c_create(&replay) == 0);
        CHECK(phase23c_import(replay, "replay.dylib", root, nroot,
                              rclasses, 2u, &image, err, sizeof(err)) == 0);
        CHECK(phase23c_allocate(replay, image, "Phase23cRoot",
                                &instance) == 0);
        CHECK(phase23c_selector(replay, "value", &method) == 0);
        CHECK(phase23c_send_words(replay, instance, method,
                                  NULL, NULL, 0u, &result) == 0);
        CHECK(result == 11u);
        CHECK(phase23c_release(replay, instance) == 0);
        CHECK(phase23c_validate(replay) == 0);
        CHECK(phase23c_destroy(replay) == 0);
    }
    (void)puts("phase23c replay ok: 64 clean runtime teardown cycles");
    free(root); free(other);
    return 0;
}
