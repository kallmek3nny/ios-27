#include "ios27/objc20a_metadata.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *load(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    assert(f);
    assert(fseek(f, 0, SEEK_END) == 0);
    long n = ftell(f);
    assert(n > 0 && n < 4 * 1024 * 1024);
    rewind(f);
    uint8_t *data = malloc((size_t)n);
    assert(data);
    assert(fread(data, 1, (size_t)n, f) == (size_t)n);
    assert(fclose(f) == 0);
    *size = (size_t)n;
    return data;
}
static const ios27_objc20a_class *cls(const ios27_objc20a_image *im,
                                      const char *name) {
    for (unsigned i = 0; i < im->class_count; ++i)
        if (!strcmp(im->classes[i].name, name)) return &im->classes[i];
    return NULL;
}
static void counts(ios27_objc20a_registry *r, size_t c, size_t p, size_t cat) {
    size_t a, b, d, sel;
    ios27_objc20a_counts(r, &a, &b, &d, &sel);
    assert(a == c && b == p && d == cat && sel <= 256);
}
static void test_real(const uint8_t *graph, size_t n,
                      const uint8_t *ext, size_t extlen,
                      const uint8_t *cat, size_t catlen) {
    ios27_objc20a_image *im = calloc(1, sizeof(*im));
    ios27_objc20a_image *imext = calloc(1, sizeof(*imext));
    ios27_objc20a_image *imcat = calloc(1, sizeof(*imcat));
    ios27_objc20a_class *copy = calloc(1, sizeof(*copy));
    ios27_objc20a_registry *r = NULL;
    char err[160] = {0};
    assert(im && imext && imcat && copy);
    assert(ios27_objc20a_inspect(graph, n, im, err, sizeof(err)) == 0);
    assert(im->class_count == 2 && im->protocol_count == 1 &&
           im->category_count == 1 && im->selector_count >= 6);
    const ios27_objc20a_class *base = cls(im, "Base"), *child = cls(im, "Child");
    assert(base && child);
    assert(!base->superclass[0] && !strcmp(child->superclass, "Base"));
    assert(base->ivar_count == 1 && child->ivar_count == 1);
    assert(child->property_count == 1 && !strcmp(child->properties[0].name, "count"));
    assert(child->protocol_count == 1 && !strcmp(child->protocols[0], "Printable"));
    assert(child->method_count == 3);
    assert(im->protocols[0].method_count == 2);
    assert(!strcmp(im->protocols[0].name, "Printable"));
    assert(!strcmp(im->categories[0].name, "Extras"));
    assert(!strcmp(im->categories[0].class_name, "Child"));
    assert(im->categories[0].method_count == 1);
    assert(ios27_objc20a_inspect(ext, extlen, imext, err, sizeof(err)) == 0);
    assert(imext->class_count == 1 &&
           !strcmp(imext->classes[0].superclass, "Base"));
    assert(ios27_objc20a_inspect(cat, catlen, imcat, err, sizeof(err)) == 0);
    assert(imcat->class_count == 0 && imcat->category_count == 1 &&
           !strcmp(imcat->categories[0].class_name, "Grand"));
    assert(ios27_objc20a_registry_create(&r) == 0);
    assert(ios27_objc20a_register(r, imext) != 0); /* missing Base */
    counts(r, 0, 0, 0);
    assert(ios27_objc20a_register(r, imcat) != 0); /* missing Grand */
    counts(r, 0, 0, 0);
    assert(ios27_objc20a_register_image(r, graph, n, err, sizeof(err)) == 0);
    counts(r, 2, 1, 1);
    assert(ios27_objc20a_find_class(r, "Child", copy) == 0);
    assert(copy->method_count == 3);
    assert(ios27_objc20a_register_image(r, graph, n, err, sizeof(err)) != 0);
    counts(r, 2, 1, 1);
    assert(ios27_objc20a_register(r, imext) == 0);
    counts(r, 3, 1, 1);
    assert(ios27_objc20a_register_image(r, cat, catlen, err, sizeof(err)) == 0);
    counts(r, 3, 1, 2);
    assert(ios27_objc20a_register(r, imcat) != 0);
    counts(r, 3, 1, 2);
    ios27_objc20a_registry_destroy(r);
    free(copy); free(im); free(imext); free(imcat);
}
static void test_corrupt(const uint8_t *file, size_t n) {
    char err[160];
    ios27_objc20a_image *out = calloc(1, sizeof(*out));
    uint8_t *m = malloc(n);
    assert(out && m);
    for (size_t length = 0; length < 32; ++length) {
        assert(ios27_objc20a_inspect(file, length, out, err, sizeof(err)) != 0);
        assert(out->class_count == 0);
    }
    memcpy(m, file, n);
    m[0] = 0; assert(ios27_objc20a_inspect(m, n, out, err, sizeof(err)) != 0);
    memcpy(m, file, n);
    memset(m + 16, 255, 8); /* excessive load commands */
    assert(ios27_objc20a_inspect(m, n, out, err, sizeof(err)) != 0);
    memcpy(m, file, n);
    m[4] = 7; /* wrong architecture */
    assert(ios27_objc20a_inspect(m, n, out, err, sizeof(err)) != 0);
    /* Repeat a broad deterministic mutation corpus under sanitizers. */
    uint32_t seed = 0x28a501u;
    for (unsigned i = 0; i < 1200; ++i) {
        memcpy(m, file, n);
        seed = seed * 1664525u + 1013904223u;
        size_t at = seed % n;
        m[at] ^= (uint8_t)(1u << (seed % 8));
        (void)ios27_objc20a_inspect(m, n, out, err, sizeof(err));
        assert(out->class_count <= IOS27_OBJC20A_CLASSES);
    }
    free(m); free(out);
}
static void test_transaction_limits(void) {
    ios27_objc20a_registry *r = NULL;
    ios27_objc20a_image *im = calloc(1, sizeof(*im));
    assert(im && ios27_objc20a_registry_create(&r) == 0);
    /* Forward-declared parents in a single batch, no runtime order dependence. */
    im->class_count = 2;
    strcpy(im->classes[0].name, "Second");
    strcpy(im->classes[0].superclass, "First");
    im->classes[0].instance_size = 8;
    strcpy(im->classes[1].name, "First");
    im->classes[1].instance_size = 8;
    assert(ios27_objc20a_register(r, im) == 0);
    counts(r, 2, 0, 0);
    strcpy(im->classes[1].superclass, "Second");
    assert(ios27_objc20a_register(r, im) != 0); /* duplicate and cycle */
    counts(r, 2, 0, 0);
    memset(im, 0, sizeof(*im));
    im->class_count = IOS27_OBJC20A_CLASSES;
    for (unsigned batch = 0; batch < 2; ++batch) {
        for (unsigned i = 0; i < im->class_count; ++i) {
            snprintf(im->classes[i].name, sizeof(im->classes[i].name),
                     "Synthetic%02u-%02u", batch, i);
            im->classes[i].instance_size = 8;
        }
        if (batch == 0) {
            /* total 34, still inside 64 */
            im->class_count = 30;
            assert(ios27_objc20a_register(r, im) == 0);
            counts(r, 32, 0, 0);
            im->class_count = IOS27_OBJC20A_CLASSES;
        } else {
            assert(ios27_objc20a_register(r, im) == 0);
            counts(r, 64, 0, 0);
        }
    }
    memset(im, 0, sizeof(*im));
    im->class_count = 1;
    strcpy(im->classes[0].name, "OneTooMany");
    im->classes[0].instance_size = 8;
    assert(ios27_objc20a_register(r, im) != 0);
    counts(r, 64, 0, 0);
    ios27_objc20a_registry_destroy(r);
    free(im);
}
int main(int argc, char **argv) {
    assert(argc == 4);
    size_t n, extlen, catlen;
    uint8_t *graph = load(argv[1], &n);
    uint8_t *ext = load(argv[2], &extlen);
    uint8_t *cat = load(argv[3], &catlen);
    test_real(graph, n, ext, extlen, cat, catlen);
    test_corrupt(graph, n);
    test_transaction_limits();
    free(graph); free(ext); free(cat);
    puts("phase20a: real clang objc2 graph, registration, malformed corpus, quotas: passed");
    return 0;
}
