#include "ios27/phase24c_registry.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static ios27_objc20a_image *im(void) {
    ios27_objc20a_image *p = calloc(1u, sizeof(*p));
    CHECK(p != NULL);
    return p;
}
static ios27_objc20a_method *method(ios27_objc20a_method *m,
                                    const char *name, const char *enc,
                                    uint64_t imp, int meta) {
    strcpy(m->name, name);
    strcpy(m->encoding, enc);
    m->implementation = imp;
    m->class_method = (uint8_t)meta;
    return m;
}
static void cls(ios27_objc20a_class *c, const char *name, const char *super) {
    strcpy(c->name, name);
    if (super) strcpy(c->superclass, super);
    c->instance_size = 32u;
}
static phase24c_registry *shared;
static void *worker(void *arg) {
    size_t id = (size_t)(uintptr_t)arg;
    for (size_t i = 0; i < 25000u; ++i) {
        phase24c_resolution resolved;
        CHECK(phase24c_resolve(shared, "Leaf", "number", 0,
                               i % 2u ? "Child" : NULL, &resolved) == 0);
        CHECK(resolved.abi == PHASE24C_WORD1);
        CHECK(resolved.image_index == (i % 2u ? 2u : 1u));
        if ((i & 1023u) == 0u) {
            phase24c_stats stats;
            CHECK(phase24c_statistics(shared, &stats) == 0);
            CHECK(stats.classes == 3u);
        }
    }
    (void)id;
    return NULL;
}
int main(void) {
    CHECK(phase24c_encoding("Q24@0:8Q16") == PHASE24C_WORD1);
    CHECK(phase24c_encoding("q40@0:8q16Q24@32") == PHASE24C_WORD3);
    CHECK(phase24c_encoding("v16@0:8") == PHASE24C_VOID0);
    CHECK(phase24c_encoding("d24@0:8d16") == PHASE24C_DOUBLE1);
    CHECK(phase24c_encoding("f24@0:8f16") == PHASE24C_FLOAT1);
    for (size_t i = 0; i < 9u; ++i) {
        const char *bad[] = {"{Pair=QQ}16@0:8", "v24@0:8Q16", "@?16@0:8",
            "Q24@0:8i16", "Q24@0:8@?16", "Q24@0:8d16", "Q16@0:8...",
            "Q16@0:8^i16", "Q24@0:8Q16junk"};
        CHECK(phase24c_encoding(bad[i]) == PHASE24C_UNSUPPORTED);
    }
    CHECK(phase24c_create(&shared) == 0);
    ios27_objc20a_image *a = im(), *b = im(), *cat = im(), *bad = im();
    a->class_count = 1u;
    cls(&a->classes[0], "Root", NULL);
    a->classes[0].method_count = 2u;
    method(&a->classes[0].methods[0], "number", "Q24@0:8Q16", 4u, 0);
    method(&a->classes[0].methods[1], "version", "Q16@0:8", 8u, 1);
    CHECK(phase24c_register(shared, a) == 0);
    b->class_count = 2u;
    cls(&b->classes[0], "Child", "Root");
    cls(&b->classes[1], "Leaf", "Child");
    b->classes[0].method_count = 2u;
    method(&b->classes[0].methods[0], "local", "Q16@0:8", 12u, 0);
    method(&b->classes[0].methods[1], "number", "Q24@0:8Q16", 20u, 0);
    b->protocol_count = 1u;
    strcpy(b->protocols[0].name, "Named");
    b->classes[0].protocol_count = 1u;
    strcpy(b->classes[0].protocols[0], "Named");
    CHECK(phase24c_register(shared, b) == 0);
    cat->category_count = 1u;
    strcpy(cat->categories[0].name, "Patch");
    strcpy(cat->categories[0].class_name, "Root");
    cat->categories[0].method_count = 1u;
    method(&cat->categories[0].methods[0], "number", "Q24@0:8Q16", 16u, 0);
    CHECK(phase24c_register(shared, cat) == 0);
    phase24c_resolution resolved;
    CHECK(phase24c_resolve(shared, "Leaf", "number", 0, NULL, &resolved) == 0);
    CHECK(resolved.category == 0u && resolved.image_index == 1u);
    CHECK(phase24c_resolve(shared, "Leaf", "number", 0, "Leaf", &resolved) == 0);
    CHECK(resolved.category == 0u && resolved.image_index == 1u);
    CHECK(phase24c_resolve(shared, "Leaf", "number", 0, "Child", &resolved) == 0);
    CHECK(phase24c_resolve(shared, "Leaf", "number", 0, "Root", &resolved) == -1);
    CHECK(phase24c_resolve(shared, "Leaf", "number", 0, "Unknown", &resolved) == -1);
    CHECK(phase24c_resolve(shared, "Leaf", "version", 1, NULL, &resolved) == 0);
    CHECK(resolved.category == 0 && resolved.image_index == 0);
    bad->class_count = 1u;
    cls(&bad->classes[0], "Root", NULL);
    CHECK(phase24c_register(shared, bad) == -1); /* duplicate */
    cls(&bad->classes[0], "Bad", "Bad");
    CHECK(phase24c_register(shared, bad) == -1); /* cycle */
    cls(&bad->classes[0], "Bad", NULL);
    bad->classes[0].method_count = 1u;
    method(&bad->classes[0].methods[0], "bad:", "Q24@0:8i16", 4u, 0);
    CHECK(phase24c_register(shared, bad) == -1); /* unsupported ABI */
    CHECK(phase24c_statistics(shared, &(phase24c_stats){0}) == 0);
    pthread_t threads[8];
    for (size_t i = 0; i < 8u; ++i)
        CHECK(pthread_create(&threads[i], NULL, worker, (void *)(uintptr_t)i) == 0);
    for (size_t i = 0; i < 8u; ++i) CHECK(pthread_join(threads[i], NULL) == 0);
    phase24c_stats stats;
    CHECK(phase24c_statistics(shared, &stats) == 0);
    CHECK(stats.lookups >= 200000u && stats.registrations == 3u &&
          stats.rejections == 3u && stats.classes == 3u);
    printf("phase24c verified: %llu lookups, %llu rejected registrations\n",
           (unsigned long long)stats.lookups,
           (unsigned long long)stats.rejections);
    phase24c_destroy(shared);
    free(a); free(b); free(cat); free(bad);
    return 0;
}
