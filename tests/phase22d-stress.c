#include "ios27/phase22d_runtime.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { WORKERS = 6, CALLS_PER_WORKER = 20000, REGISTER_THREADS = 8 };
typedef struct {
    phase22d_runtime *runtime;
    void *receiver;
    phase22d_sel selector;
    atomic_int *errors;
} worker_args;
static atomic_int root_inits, child_inits, failures, destructors;
static phase22d_runtime *initializing_runtime;
static phase22d_sel init_selector;

static uintptr_t root_value(void *receiver, phase22d_sel sel) {
    (void)receiver; (void)sel; return 11;
}
static uintptr_t category_value(void *receiver, phase22d_sel sel) {
    (void)receiver; (void)sel; return 77;
}
static uintptr_t ping(void *receiver, phase22d_sel sel) {
    (void)receiver; (void)sel; return 91;
}
static uintptr_t add(void *receiver, phase22d_sel sel, uintptr_t a) {
    (void)receiver; (void)sel; return a + 20u;
}
static uintptr_t sum4(void *receiver, phase22d_sel sel, uintptr_t a,
                      uintptr_t b, uintptr_t c, uintptr_t d) {
    (void)receiver; (void)sel; return a + b + c + d;
}
static double square(void *receiver, phase22d_sel sel, double v) {
    (void)receiver; (void)sel; return v * v;
}
static void destroyed(void *obj, void *context) {
    (void)obj; (void)context;
    atomic_fetch_add(&destructors, 1);
}
static int initialize_root(void *context, void *cls) {
    (void)context;
    uintptr_t result = 0;
    /* Callback reentrancy is intentionally exercised, not bypassed. */
    if (phase22d_send_words(initializing_runtime, cls, init_selector,
                            NULL, NULL, 0, &result) || result != 91)
        return -1;
    atomic_fetch_add(&root_inits, 1);
    return 0;
}
static int initialize_child(void *context, void *cls) {
    (void)context; (void)cls;
    if (atomic_load(&root_inits) != 1) return -1;
    atomic_fetch_add(&child_inits, 1);
    return 0;
}
static int failing_initializer(void *context, void *cls) {
    (void)context; (void)cls;
    return atomic_fetch_add(&failures, 1) == 0 ? -1 : 0;
}
static void *send_worker(void *arg) {
    const worker_args *a = arg;
    uintptr_t result = 0;
    for (int i = 0; i < CALLS_PER_WORKER; ++i) {
        if (phase22d_retain(a->runtime, a->receiver) ||
            phase22d_send_words(a->runtime, a->receiver, a->selector,
                                NULL, NULL, 0, &result) ||
            (result != 11 && result != 77) ||
            phase22d_release(a->runtime, a->receiver))
            atomic_fetch_add(a->errors, 1);
    }
    return NULL;
}
typedef struct {
    phase22d_runtime *runtime;
    unsigned id;
    atomic_int *errors;
} registration_args;
static void *register_worker(void *arg) {
    const registration_args *a = arg;
    char name[32];
    (void)snprintf(name, sizeof(name), "Concurrent%u", a->id);
    phase22d_class_spec cls = {0};
    cls.name = name;
    cls.superclass = "Root";
    cls.instance_size = 2u * sizeof(void *);
    if (phase22d_register(a->runtime, &cls, 1))
        atomic_fetch_add(a->errors, 1);
    return NULL;
}
#define CHECK(x) do { if (!(x)) { \
    (void)fprintf(stderr, "check failed at %s:%d: %s\\n", __FILE__, __LINE__, #x); \
    return 1; } } while (0)
#define M0(s, f) {s, PHASE22D_WORD0, {.word0 = f}}
#define M1(s, f) {s, PHASE22D_WORD1, {.word1 = f}}
#define M4(s, f) {s, PHASE22D_WORD4, {.word4 = f}}
#define MD(s, f) {s, PHASE22D_DOUBLE1, {.double1 = f}}

int main(void) {
    phase22d_runtime *r = NULL;
    uintptr_t result = 0, words[] = {1, 2, 3, 4};
    double real = 0;
    void *object = NULL, *class_obj = NULL, *meta_obj = NULL, *weak = NULL;
    phase22d_sel sel_value, sel_ping, sel_add, sel_sum4, sel_square;
    pthread_t registrations[REGISTER_THREADS], workers[WORKERS];
    registration_args reg_args[REGISTER_THREADS];
    worker_args args[WORKERS];
    atomic_int errors = 0;
    phase22d_stats stats;
    CHECK(phase22d_create(&r) == 0);
    initializing_runtime = r;
    const phase22d_method_spec root_instance[] = {
        M0("value", root_value), M1("add:", add), M4("sum::::", sum4),
        MD("square:", square)
    };
    const phase22d_method_spec root_class[] = {M0("ping", ping)};
    const phase22d_class_spec classes[] = {
        {.name = "Child", .superclass = "Root",
         .instance_size = 2u * sizeof(void *),
         .initialize = initialize_child, .deallocate = destroyed},
        {.name = "Root", .instance_size = sizeof(void *),
         .instance_methods = root_instance, .instance_count = 4u,
         .class_methods = root_class, .class_count = 1u,
         .initialize = initialize_root}
    };
    CHECK(phase22d_register(r, classes, 2) == 0); /* forward parent */
    CHECK(phase22d_selector(r, "ping", &init_selector) == 0);
    sel_ping = init_selector;
    CHECK(phase22d_selector(r, "value", &sel_value) == 0);
    CHECK(phase22d_selector(r, "add:", &sel_add) == 0);
    CHECK(phase22d_selector(r, "sum::::", &sel_sum4) == 0);
    CHECK(phase22d_selector(r, "square:", &sel_square) == 0);
    CHECK(phase22d_class_object(r, "Root", &class_obj) == 0);
    CHECK(phase22d_metaclass_object(r, "Root", &meta_obj) == 0);
    CHECK(class_obj != meta_obj);
    CHECK(phase22d_send_words(r, class_obj, sel_ping, NULL, NULL, 0, &result) == 0);
    CHECK(result == 91 && atomic_load(&root_inits) == 1);
    CHECK(phase22d_send_words(r, meta_obj, sel_ping, NULL, NULL, 0, &result) == 0);
    CHECK(phase22d_allocate(r, "Child", &object) == 0);
    CHECK(phase22d_send_words(r, object, sel_value, NULL, NULL, 0, &result) == 0);
    CHECK(result == 11 && atomic_load(&child_inits) == 1);
    CHECK(phase22d_send_words(r, object, sel_add, NULL, words, 1, &result) == 0);
    CHECK(result == 21);
    CHECK(phase22d_send_words(r, object, sel_sum4, NULL, words, 4, &result) == 0);
    CHECK(result == 10);
    CHECK(phase22d_send_double1(r, object, sel_square, NULL, 1.5, &real) == 0);
    CHECK(real == 2.25);
    /* No implicit widening or mismatched ABI calls. */
    CHECK(phase22d_send_words(r, object, sel_square, NULL, words, 1, &result) != 0);
    CHECK(phase22d_send_double1(r, object, sel_value, NULL, 2.0, &real) != 0);
    CHECK(phase22d_send_words(r, object, sel_add, NULL, NULL, 0, &result) != 0);
    CHECK(phase22d_send_words(r, object, (phase22d_sel)0x1234u,
                             NULL, NULL, 0, &result) != 0);
    CHECK(phase22d_send_words(r, object, sel_value, "Root",
                              NULL, 0, &result) != 0);
    CHECK(phase22d_send_words(r, object, sel_value, "Child",
                              NULL, 0, &result) == 0 && result == 11);
    /* Failed metadata/class transactions never partially register. */
    const phase22d_class_spec invalid[] = {
        {.name = "CycleA", .superclass = "CycleB", .instance_size = 16},
        {.name = "CycleB", .superclass = "CycleA", .instance_size = 16}
    };
    CHECK(phase22d_register(r, invalid, 2) != 0);
    CHECK(phase22d_class_object(r, "CycleA", &class_obj) != 0);
    const phase22d_class_spec dup = {.name = "Root", .instance_size = 16};
    CHECK(phase22d_register(r, &dup, 1) != 0);
    const phase22d_class_spec missing =
        {.name = "Orphan", .superclass = "Unknown", .instance_size = 16};
    CHECK(phase22d_register(r, &missing, 1) != 0);
    const phase22d_method_spec bad_methods[] = {
        {.selector = "bad", .signature = PHASE22D_DOUBLE1,
         .implementation = {.double1 = NULL}}
    };
    const phase22d_class_spec bad =
        {.name = "Bad", .instance_size = 16,
         .instance_methods = bad_methods, .instance_count = 1};
    CHECK(phase22d_register(r, &bad, 1) != 0);
    /* Concurrent distinct-class registration is serialized and bounded. */
    for (unsigned i = 0; i < REGISTER_THREADS; ++i) {
        reg_args[i] = (registration_args){r, i, &errors};
        CHECK(!pthread_create(&registrations[i], NULL,
                              register_worker, &reg_args[i]));
    }
    for (unsigned i = 0; i < REGISTER_THREADS; ++i)
        CHECK(!pthread_join(registrations[i], NULL));
    CHECK(atomic_load(&errors) == 0);
    const phase22d_class_spec flaky =
        {.name = "Flaky", .instance_size = sizeof(void *),
         .initialize = failing_initializer,
         .instance_methods = root_instance, .instance_count = 1};
    CHECK(phase22d_register(r, &flaky, 1) == 0);
    void *flaky_obj = NULL;
    CHECK(phase22d_allocate(r, "Flaky", &flaky_obj) == 0);
    CHECK(phase22d_send_words(r, flaky_obj, sel_value, NULL, NULL, 0, &result) != 0);
    CHECK(phase22d_send_words(r, flaky_obj, sel_value, NULL, NULL, 0, &result) == 0);
    CHECK(atomic_load(&failures) == 2);
    CHECK(phase22d_release(r, flaky_obj) == 0);
    CHECK(phase22d_weak_store(r, &weak, object) == 0);
    CHECK(weak == object);
    void *locked = NULL;
    CHECK(phase22d_weak_load_retained(r, &weak, &locked) == 0);
    CHECK(locked == object && phase22d_release(r, locked) == 0);
    /* 120k method sends across six threads with 240k explicit retain/release
     * operations plus automatic dispatch pins; rejects are checked, not masked. */
    for (unsigned i = 0; i < WORKERS; ++i) {
        args[i] = (worker_args){r, object, sel_value, &errors};
        CHECK(!pthread_create(&workers[i], NULL, send_worker, &args[i]));
    }
    const phase22d_method_spec replacement[] = {M0("value", category_value)};
    const phase22d_category cat =
        {.class_name = "Child", .methods = replacement, .count = 1};
    CHECK(phase22d_install_category(r, &cat) == 0);
    for (unsigned i = 0; i < WORKERS; ++i)
        CHECK(!pthread_join(workers[i], NULL));
    CHECK(atomic_load(&errors) == 0);
    CHECK(phase22d_send_words(r, object, sel_value, NULL, NULL, 0, &result) == 0);
    CHECK(result == 77);
    CHECK(phase22d_send_words(r, object, sel_value, "Child",
                              NULL, 0, &result) == 0 && result == 11);
    CHECK(phase22d_release(r, object) == 0);
    CHECK(weak == NULL && atomic_load(&destructors) == 1);
    CHECK(phase22d_retain(r, object) != 0);
    CHECK(phase22d_weak_load_retained(r, &weak, &locked) == 0 && locked == NULL);
    CHECK(phase22d_weak_unregister(r, &weak) == 0);
    CHECK(phase22d_validate(r) == 0);
    CHECK(phase22d_statistics(r, &stats) == 0);
    CHECK(stats.live_objects == 0 && stats.weak_locations == 0);
    CHECK(stats.sends >= (uint64_t)WORKERS * CALLS_PER_WORKER);
    CHECK(stats.initializations >= 3 && stats.categories == 1);
    (void)printf("phase22d ok: sends=%llu failures=%llu init=%llu waits=%llu "
                 "alloc=%llu dealloc=%llu classes=%zu selectors=%zu\\n",
                 (unsigned long long)stats.sends,
                 (unsigned long long)stats.failed_sends,
                 (unsigned long long)stats.initializations,
                 (unsigned long long)stats.init_waits,
                 (unsigned long long)stats.allocations,
                 (unsigned long long)stats.destroyed,
                 stats.classes, stats.selectors);
    phase22d_destroy(r);
    return 0;
}
