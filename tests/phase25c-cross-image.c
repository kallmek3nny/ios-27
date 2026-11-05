#include "ios27/phase25c_lifetime.h"

#include <assert.h>
#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int (*ref_fn)(ios27_phase25c_runtime *, void *);
typedef int (*weak_fn)(ios27_phase25c_runtime *, void **, void *);
typedef int (*load_fn)(ios27_phase25c_runtime *, void **, void **);
static int isa_token;
static unsigned finalized;
typedef struct { void *isa; void *child; } node;
static const ios27_phase25c_class klass = {
    &isa_token, 25, sizeof(node), _Alignof(node),
    offsetof(node, child), sizeof(void *)
};
static void dealloc(ios27_phase25c_runtime *rt, void *p, void *ctx) {
    (void)rt; (void)p; (void)ctx;
    ++finalized;
}
static void lookup(void *library, const char *symbol, void *target, size_t size) {
    void *entry = dlsym(library, symbol);
    assert(entry != NULL && size == sizeof(entry));
    /* ISO C disallows a direct cast from data to function pointers. POSIX
     * dlsym supplies a machine code pointer of equal representation. */
    memcpy(target, &entry, size);
}
int main(int argc, char **argv) {
    assert(argc == 3);
    void *image_a = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    void *image_b = dlopen(argv[2], RTLD_NOW | RTLD_LOCAL);
    if (!image_a || !image_b) fprintf(stderr, "dlopen: %s\n", dlerror());
    assert(image_a && image_b && image_a != image_b);
    ref_fn retain_a = NULL, release_b = NULL;
    weak_fn weak_a = NULL;
    load_fn load_b = NULL;
    lookup(image_a, "phase25c_image_a_retain", &retain_a, sizeof(retain_a));
    lookup(image_a, "phase25c_image_a_weak", &weak_a, sizeof(weak_a));
    lookup(image_b, "phase25c_image_b_release", &release_b, sizeof(release_b));
    lookup(image_b, "phase25c_image_b_load", &load_b, sizeof(load_b));
    ios27_phase25c_runtime *rt = NULL;
    assert(ios27_phase25c_create(&rt) == 0);
    void *object = NULL, *weak = NULL;
    assert(ios27_phase25c_alloc(rt, &klass, dealloc, NULL, &object) == 0);
    assert(weak_a(rt, &weak, object) == 0);
    for (unsigned i = 0; i < 30000; ++i) {
        assert(retain_a(rt, object) == 0);
        void *held = NULL;
        assert(load_b(rt, &weak, &held) == 0 && held == object);
        assert(release_b(rt, held) == 0);
        assert(release_b(rt, object) == 0);
    }
    assert(ios27_phase25c_can_unload(rt, klass.isa, klass.image_generation) == -1);
    assert(release_b(rt, object) == 0);
    assert(weak == NULL && finalized == 1);
    void *out = (void *)1;
    assert(load_b(rt, &weak, &out) == 0 && out == NULL);
    assert(ios27_phase25c_destroy_weak(rt, &weak) == 0);
    assert(ios27_phase25c_can_unload(rt, klass.isa, klass.image_generation) == 0);
    assert(ios27_phase25c_validate(rt) == 0);
    assert(ios27_phase25c_destroy(&rt) == 0);
    assert(dlclose(image_a) == 0);
    assert(dlclose(image_b) == 0);
    puts("phase25c: two independently linked host images share lifetime identity");
    return 0;
}
