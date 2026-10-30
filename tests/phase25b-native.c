#define _POSIX_C_SOURCE 200809L
#include "ios27/phase25b_dispatch.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(__aarch64__)
#error "The native Darwin ARM64 method execution test requires an AArch64 host"
#endif

/* GNU asm labels provide exact fixed FP signatures without an indirect
 * function pointer cast or disabling UBSan's function-type instrumentation. */
extern double send_double(void *, phase25b_sel, double)
    __asm__("phase25b_objc_msgSend");
extern float send_float(void *, phase25b_sel, float)
    __asm__("phase25b_objc_msgSend");
static phase25b_runtime *runtime;
static void *root, *child, *leaf;
static phase25b_sel add, kind, twice, triple, echo, six;
static uint32_t images[8];
static void *child_obj;
static uint64_t direct_ns, dispatch_ns;

static uint64_t ns(void) {
    struct timespec ts;
    assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return (uint64_t)ts.tv_sec*1000000000ull + (uint64_t)ts.tv_nsec;
}
static void load(const char *directory, const char *name, uint32_t *slot) {
    char path[1024];
    int len = snprintf(path, sizeof(path), "%s/%s.bin", directory, name);
    assert(len > 0 && (size_t)len < sizeof(path));
    FILE *f = fopen(path, "rb");
    assert(f);
    uint8_t code[128] = {0};
    size_t bytes = fread(code, 1, sizeof(code), f);
    assert(fclose(f) == 0);
    assert(bytes >= 8 && bytes <= 16 && (bytes % 4u) == 0);
    assert(phase25b_publish_image(runtime, code, bytes, slot) == 0);
}
static void *thread_work(void *argument) {
    uintptr_t worker = (uintptr_t)argument;
    for (uintptr_t i = 0; i < 30000u; ++i) {
        uintptr_t n = i + worker;
        uintptr_t result = phase25b_objc_msgSend(child_obj, add, n);
        assert(result == n + 20u);
        phase25b_super sup = {child_obj, child};
        result = phase25b_objc_msgSendSuper2(&sup, add, n);
        assert(result == n+5u || result == n+9u);
        double doubled = send_double(child_obj, twice, 1.25);
        assert(doubled == 2.5);
    }
    return NULL;
}
static void test_direct_benchmark(void) {
    phase25b_ticket ticket;
    phase25b_super sup = {child_obj, child};
    void *imp = phase25b_prepare(&sup, add, &ticket, 1);
    assert(imp != NULL);
    volatile uintptr_t result = 0;
    uint64_t begin = ns();
    for (uintptr_t i = 0; i < 50000; ++i)
        result = phase25b_direct_word1(child_obj, add, i, imp);
    direct_ns = ns()-begin;
    phase25b_finish(&ticket);
    assert(result == (uintptr_t)49999+5u || result == (uintptr_t)49999+9u);
    begin = ns();
    for (uintptr_t i = 0; i < 50000; ++i)
        result = phase25b_objc_msgSend(child_obj, add, i);
    dispatch_ns = ns()-begin;
    assert(result == (uintptr_t)49999+20u);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    assert(phase25b_create(&runtime) == 0);
    assert(phase25b_register_class(runtime, "P25Root", NULL) == 0);
    assert(phase25b_register_class(runtime, "P25Child", "P25Root") == 0);
    assert(phase25b_register_class(runtime, "P25Leaf", "P25Child") == 0);
    assert(phase25b_class_token(runtime, "P25Root", 0, &root) == 0);
    assert(phase25b_class_token(runtime, "P25Child", 0, &child) == 0);
    assert(phase25b_class_token(runtime, "P25Leaf", 0, &leaf) == 0);
    assert(phase25b_selector(runtime, "add:", &add) == 0);
    assert(phase25b_selector(runtime, "kind", &kind) == 0);
    assert(phase25b_selector(runtime, "twice:", &twice) == 0);
    assert(phase25b_selector(runtime, "triple:", &triple) == 0);
    assert(phase25b_selector(runtime, "echo:", &echo) == 0);
    assert(phase25b_selector(runtime, "six:b:c:d:e:f:", &six) == 0);
    const char *names[] = {
        "kind", "add", "twice", "triple", "echo", "six", "child", "category"
    };
    for (size_t i = 0; i < 8; ++i) load(argv[1], names[i], &images[i]);
    assert(phase25b_method(runtime, "P25Root", 1, "kind", "Q16@0:8", images[0], 0, 0) == 0);
    assert(phase25b_method(runtime, "P25Root", 0, "add:", "Q24@0:8Q16", images[1], 0, 0) == 0);
    assert(phase25b_method(runtime, "P25Root", 0, "twice:", "d24@0:8d16", images[2], 0, 0) == 0);
    assert(phase25b_method(runtime, "P25Root", 0, "triple:", "f24@0:8f16", images[3], 0, 0) == 0);
    assert(phase25b_method(runtime, "P25Root", 0, "echo:", "^v24@0:8^v16", images[4], 0, 0) == 0);
    assert(phase25b_method(runtime, "P25Root", 0, "six:b:c:d:e:f:", "Q64@0:8Q16Q24Q32Q40Q48Q56", images[5], 0, 0) == 0);
    assert(phase25b_method(runtime, "P25Child", 0, "add:", "Q24@0:8Q16", images[6], 0, 0) == 0);
    child_obj = phase25b_alloc(runtime, "P25Child");
    void *leaf_obj = phase25b_alloc(runtime, "P25Leaf");
    assert(child_obj && leaf_obj);
    assert(phase25b_set_active(runtime) == 0);
    assert(phase25b_objc_msgSend(root, kind) == 25);
    assert(phase25b_objc_msgSend(child_obj, kind) == 0);
    assert(phase25b_last_status() == PHASE25B_BAD_METHOD);
    assert(phase25b_objc_msgSend(child_obj, add, (uintptr_t)10) == 30);
    assert(phase25b_objc_msgSend(leaf_obj, add, (uintptr_t)10) == 30);
    phase25b_super sup = {child_obj, child};
    assert(phase25b_objc_msgSendSuper2(&sup, add, (uintptr_t)10) == 15);
    sup.current_class = root;
    assert(phase25b_objc_msgSendSuper2(&sup, add, (uintptr_t)10) == 0);
    assert(phase25b_last_status() == PHASE25B_BAD_SUPER);
    assert(phase25b_objc_msgSend(NULL, add, (uintptr_t)10) == 0);
    assert(phase25b_last_status() == PHASE25B_NIL);
    assert(phase25b_objc_msgSend(child_obj, echo, (void *)child_obj) == (uintptr_t)child_obj);
    assert(phase25b_objc_msgSend(child_obj, six, (uintptr_t)1, (uintptr_t)2,
                                 (uintptr_t)3, (uintptr_t)4, (uintptr_t)5,
                                 (uintptr_t)6) == 7);
    assert(send_double(child_obj, twice, 1.5) == 3.0);
    assert(send_float(child_obj, triple, 2.0f) == 6.0f);
    test_direct_benchmark();
    pthread_t threads[8];
    for (uintptr_t i = 0; i < 8; ++i)
        assert(pthread_create(&threads[i], NULL, thread_work, (void *)i) == 0);
    for (size_t i = 0; i < 1000; ++i) {
        uint32_t code = (i & 1u) ? images[1] : images[7];
        assert(phase25b_method(runtime, "P25Root", 0, "add:",
                               "Q24@0:8Q16", code, 0, 1) == 0);
    }
    for (size_t i = 0; i < 8; ++i)
        assert(pthread_join(threads[i], NULL) == 0);
    phase25b_stats stats;
    phase25b_statistics(runtime, &stats);
    assert(stats.sends >= 720000 && stats.hits > 1000 && stats.misses > 0);
    assert(stats.in_flight == 0);
    assert(phase25b_release(runtime, child_obj) == 0);
    assert(phase25b_release(runtime, leaf_obj) == 0);
    assert(phase25b_set_active(NULL) == 0);
    assert(phase25b_destroy(runtime) == 0);
    printf("phase25b: %llu REAL compiler-produced linked Darwin ARM64 method sends, "
           "super sends, FP and x2-x7 verified; hits=%llu misses=%llu\n",
           (unsigned long long)stats.sends,
           (unsigned long long)stats.hits, (unsigned long long)stats.misses);
    printf("phase25b: 50k direct IMP=%llu ns; 50k objc_msgSend=%llu ns "
           "(method mix differs; indicative only)\n",
           (unsigned long long)direct_ns, (unsigned long long)dispatch_ns);
    return 0;
}
