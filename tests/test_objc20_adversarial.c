/* Z1 t-8da2da: host-side adversarial tests for the phase 20 Objective-C
 * substrate: the objc20a metadata registry, objc20b dispatch and objc20c
 * lifetime runtimes. Synthetic metadata only; no guest code is mapped or
 * executed. Every reject must return -1 and leave the observable registry
 * or runtime state unchanged. Cases that tests/phase20a-*, phase20b-*,
 * phase20c-* or L t-563cdd already cover are not repeated here. */
#include "ios27/objc20a_metadata.h"
#include "ios27/objc20b_dispatch.h"
#include "ios27/objc20c_lifetime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Private registry capacities from src/objc20a_registry.c (REG_*). */
#define A_REG_PROTOCOLS 64u
#define A_REG_CATEGORIES 64u
#define A_REG_SELECTORS 256u

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

/* ---- objc20a metadata registry ---- */

typedef struct {
    size_t classes, protocols, categories, selectors;
} a_counts;

static a_counts a_snapshot(const ios27_objc20a_registry *r) {
    a_counts c;
    ios27_objc20a_counts(r, &c.classes, &c.protocols, &c.categories,
                         &c.selectors);
    return c;
}

static int a_same(a_counts x, a_counts y) {
    return x.classes == y.classes && x.protocols == y.protocols &&
           x.categories == y.categories && x.selectors == y.selectors;
}

static void a_name(char *dst, const char *prefix, unsigned n) {
    snprintf(dst, IOS27_OBJC20A_NAME, "%s%u", prefix, n);
}

static void a_add_classes(ios27_objc20a_image *im, const char *prefix,
                          unsigned first, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        ios27_objc20a_class *c = &im->classes[im->class_count++];
        a_name(c->name, prefix, first + i);
        c->instance_size = 8u;
    }
}

static void a_add_protocols(ios27_objc20a_image *im, const char *prefix,
                            unsigned first, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        a_name(im->protocols[im->protocol_count++].name, prefix, first + i);
}

static void a_add_categories(ios27_objc20a_image *im, const char *cls,
                             const char *prefix, unsigned first,
                             unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        ios27_objc20a_category *c = &im->categories[im->category_count++];
        a_name(c->name, prefix, first + i);
        snprintf(c->class_name, sizeof(c->class_name), "%s", cls);
    }
}

static void a_add_selectors(ios27_objc20a_image *im, const char *prefix,
                            unsigned first, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        a_name(im->selectors[im->selector_count++], prefix, first + i);
}

static void test_objc20a(void) {
    ios27_objc20a_registry *r = NULL;
    ios27_objc20a_image *im = calloc(1u, sizeof(*im));
    ios27_objc20a_class *found = calloc(1u, sizeof(*found));
    uint8_t junk[64];
    char err[128] = "";
    a_counts before, none;

    CHECK(ios27_objc20a_registry_create(NULL) == -1);
    if (!im || !found || ios27_objc20a_registry_create(&r) != 0 || !r) {
        setup_failed("objc20a registry or image allocation");
        ios27_objc20a_registry_destroy(r);
        free(found);
        free(im);
        return;
    }

    /* NULL registry and image arguments. */
    none = a_snapshot(NULL);
    CHECK(none.classes == 0u && none.protocols == 0u &&
          none.categories == 0u && none.selectors == 0u);
    a_add_classes(im, "Root", 0u, 1u);
    CHECK(ios27_objc20a_register(NULL, im) == -1);
    CHECK(ios27_objc20a_register(r, NULL) == -1);
    CHECK(a_same(none, a_snapshot(r)));
    memset(junk, 0, sizeof(junk));
    CHECK(ios27_objc20a_register_image(NULL, junk, sizeof(junk), err,
                                       sizeof(err)) == -1);
    CHECK(ios27_objc20a_register_image(r, junk, sizeof(junk), err,
                                       sizeof(err)) != 0);
    CHECK(err[0] != '\0');
    CHECK(a_same(none, a_snapshot(r)));

    /* find_class rejects NULL arguments and unknown names. */
    CHECK(ios27_objc20a_register(r, im) == 0);
    CHECK(ios27_objc20a_find_class(NULL, "Root0", found) == -1);
    CHECK(ios27_objc20a_find_class(r, NULL, found) == -1);
    CHECK(ios27_objc20a_find_class(r, "Root0", NULL) == -1);
    CHECK(ios27_objc20a_find_class(r, "Missing", found) == -1);
    CHECK(ios27_objc20a_find_class(r, "", found) == -1);
    CHECK(ios27_objc20a_find_class(r, "Root0", found) == 0 &&
          strcmp(found->name, "Root0") == 0);

    /* Per-image counts past the header caps. */
    before = a_snapshot(r);
    CHECK(before.classes == 1u);
    memset(im, 0, sizeof(*im));
    im->class_count = IOS27_OBJC20A_CLASSES + 1u;
    CHECK(ios27_objc20a_register(r, im) == -1);
    memset(im, 0, sizeof(*im));
    im->protocol_count = IOS27_OBJC20A_PROTOCOLS + 1u;
    CHECK(ios27_objc20a_register(r, im) == -1);
    memset(im, 0, sizeof(*im));
    im->category_count = IOS27_OBJC20A_CATEGORIES + 1u;
    CHECK(ios27_objc20a_register(r, im) == -1);
    memset(im, 0, sizeof(*im));
    im->selector_count = IOS27_OBJC20A_SELECTORS + 1u;
    CHECK(ios27_objc20a_register(r, im) == -1);
    CHECK(a_same(before, a_snapshot(r)));

    /* A late category failure rolls back the protocol and class staged
     * earlier in the same image. */
    memset(im, 0, sizeof(*im));
    a_add_protocols(im, "AtomP", 0u, 1u);
    a_add_classes(im, "Atom", 0u, 1u);
    a_add_categories(im, "Nowhere", "AtomCat", 0u, 1u);
    CHECK(ios27_objc20a_register(r, im) == -1);
    CHECK(a_same(before, a_snapshot(r)));
    CHECK(ios27_objc20a_find_class(r, "Atom0", found) == -1);

    /* Selector table: exactly full, re-intern at capacity, one past. */
    for (unsigned batch = 0; batch < A_REG_SELECTORS / IOS27_OBJC20A_SELECTORS;
         ++batch) {
        memset(im, 0, sizeof(*im));
        a_add_selectors(im, "s", batch * IOS27_OBJC20A_SELECTORS,
                        IOS27_OBJC20A_SELECTORS);
        CHECK(ios27_objc20a_register(r, im) == 0);
    }
    before = a_snapshot(r);
    CHECK(before.selectors == A_REG_SELECTORS);
    memset(im, 0, sizeof(*im));
    a_add_selectors(im, "s", 0u, 1u);
    CHECK(ios27_objc20a_register(r, im) == 0);
    CHECK(a_same(before, a_snapshot(r)));
    memset(im, 0, sizeof(*im));
    a_add_classes(im, "Late", 0u, 1u);
    a_add_selectors(im, "s", A_REG_SELECTORS, 1u);
    CHECK(ios27_objc20a_register(r, im) == -1);
    CHECK(a_same(before, a_snapshot(r)));
    CHECK(ios27_objc20a_find_class(r, "Late0", found) == -1);

    /* Protocol table: exactly full, then one past. */
    for (unsigned batch = 0; batch < A_REG_PROTOCOLS / IOS27_OBJC20A_PROTOCOLS;
         ++batch) {
        memset(im, 0, sizeof(*im));
        a_add_protocols(im, "P", batch * IOS27_OBJC20A_PROTOCOLS,
                        IOS27_OBJC20A_PROTOCOLS);
        CHECK(ios27_objc20a_register(r, im) == 0);
    }
    before = a_snapshot(r);
    CHECK(before.protocols == A_REG_PROTOCOLS);
    memset(im, 0, sizeof(*im));
    a_add_protocols(im, "P", A_REG_PROTOCOLS, 1u);
    CHECK(ios27_objc20a_register(r, im) == -1);
    CHECK(a_same(before, a_snapshot(r)));

    /* Category table: exactly full on Root0, then one past. */
    for (unsigned batch = 0;
         batch < A_REG_CATEGORIES / IOS27_OBJC20A_CATEGORIES; ++batch) {
        memset(im, 0, sizeof(*im));
        a_add_categories(im, "Root0", "Cat", batch * IOS27_OBJC20A_CATEGORIES,
                         IOS27_OBJC20A_CATEGORIES);
        CHECK(ios27_objc20a_register(r, im) == 0);
    }
    before = a_snapshot(r);
    CHECK(before.categories == A_REG_CATEGORIES);
    memset(im, 0, sizeof(*im));
    a_add_categories(im, "Root0", "Cat", A_REG_CATEGORIES, 1u);
    CHECK(ios27_objc20a_register(r, im) == -1);
    CHECK(a_same(before, a_snapshot(r)));

    ios27_objc20a_registry_destroy(r);
    free(found);
    free(im);
}

/* ---- objc20b dispatch ---- */

#define AS_IMP(f) ((ios27_objc20b_imp)(void (*)(void))(f))

static unsigned char class_tokens[2u * (IOS27_OBJC20B_CLASSES + 1u)];
static unsigned char foreign_object[32];
static char class_names[IOS27_OBJC20B_CLASSES + 1u][16];
static char selector_names[IOS27_OBJC20B_SELECTORS + 1u][16];
static ios27_objc20b_class class_specs[IOS27_OBJC20B_CLASSES + 1u];

static uintptr_t imp_seven(void *self, ios27_objc20b_sel sel) {
    (void)self;
    (void)sel;
    return 7u;
}

/* Class and metaclass identities are distinct addresses inside
 * class_tokens; the runtime compares them and never dereferences them. */
static ios27_objc20b_class b_spec(unsigned slot, const char *name,
                                  const ios27_objc20b_method *list,
                                  size_t count) {
    ios27_objc20b_class c;
    memset(&c, 0, sizeof(c));
    c.name = name;
    c.class_object = &class_tokens[2u * slot];
    c.metaclass_object = &class_tokens[2u * slot + 1u];
    c.instance_size = 16u;
    c.instances = list;
    c.instance_count = count;
    return c;
}

static int b_same(const ios27_objc20b_stats *x, const ios27_objc20b_stats *y) {
    return x->epoch == y->epoch && x->classes == y->classes &&
           x->selectors == y->selectors && x->objects == y->objects;
}

static void test_objc20b(void) {
    const ios27_objc20b_method foo_method[1] = {
        {"foo", AS_IMP(imp_seven), 0u}};
    const ios27_objc20b_method wide_method[1] = {
        {"wide", AS_IMP(imp_seven), 5u}};
    ios27_objc20b_method replacement = {"foo", AS_IMP(imp_seven), 0u};
    ios27_objc20b_class batch[2];
    ios27_objc20b_runtime *r = NULL;
    ios27_objc20b_stats before, after;
    ios27_objc20b_sel foo = NULL, bar = NULL;
    ios27_objc20b_imp imp = NULL;
    uintptr_t five[5] = {1u, 2u, 3u, 4u, 5u};
    uintptr_t result = 0u, stale;
    uint8_t argc = 0u;
    void *object;

    CHECK(ios27_objc20b_create(NULL) == -1);
    if (ios27_objc20b_create(&r) != 0 || !r) {
        setup_failed("objc20b runtime allocation");
        return;
    }
    batch[0] = b_spec(0u, "A", foo_method, 1u);
    batch[1] = b_spec(1u, "Wide", wide_method, 1u);

    /* NULL runtime and argument rejects. */
    ios27_objc20b_statistics(NULL, &before);
    CHECK(before.classes == 0u && before.selectors == 0u &&
          before.objects == 0u && before.epoch == 0u);
    ios27_objc20b_statistics(r, &before);
    CHECK(ios27_objc20b_register(NULL, batch, 1u) == -1);
    CHECK(ios27_objc20b_register(r, NULL, 1u) == -1);
    CHECK(ios27_objc20b_register(r, batch, 0u) == -1);
    CHECK(ios27_objc20b_selector(NULL, "foo", &foo) == -1);
    CHECK(ios27_objc20b_selector(r, "foo", NULL) == -1);
    CHECK(ios27_objc20b_selector(r, NULL, &foo) == -1);
    CHECK(ios27_objc20b_alloc(NULL, "A") == NULL);
    CHECK(ios27_objc20b_release(NULL, foreign_object) == -1);
    CHECK(ios27_objc20b_release(r, NULL) == -1);

    /* word_arguments 5, alone and as the bad half of a mixed batch. The
     * good class and its selector must not leak from the failed batch. */
    CHECK(ios27_objc20b_register(r, &batch[1], 1u) == -1);
    CHECK(ios27_objc20b_register(r, batch, 2u) == -1);
    ios27_objc20b_statistics(r, &after);
    CHECK(b_same(&before, &after));
    CHECK(ios27_objc20b_alloc(r, "A") == NULL);
    CHECK(ios27_objc20b_register(r, batch, 1u) == 0);

    CHECK(ios27_objc20b_selector(r, "foo", &foo) == 0);
    CHECK(ios27_objc20b_selector(r, "bar", &bar) == 0);
    object = ios27_objc20b_alloc(r, "A");
    if (!object) {
        setup_failed("objc20b alloc of A");
        ios27_objc20b_destroy(r);
        return;
    }

    /* Positive controls, so the rejects below are meaningful. */
    CHECK(ios27_objc20b_resolve(r, object, foo, NULL, &imp, &argc) == 0 &&
          imp == AS_IMP(imp_seven) && argc == 0);
    CHECK(ios27_objc20b_send_words(r, object, foo, NULL, NULL, 0u,
                                   &result) == 0 && result == 7u);
    ios27_objc20b_statistics(r, &before);

    /* nil receiver. */
    imp = AS_IMP(imp_seven);
    argc = 9u;
    CHECK(ios27_objc20b_resolve(r, NULL, foo, NULL, &imp, &argc) == -1);
    CHECK(!imp && argc == 0);
    CHECK(ios27_objc20b_send_words(r, NULL, foo, NULL, NULL, 0u,
                                   &result) == -1);

    /* Interned selector with no IMP anywhere in the hierarchy. */
    imp = AS_IMP(imp_seven);
    CHECK(ios27_objc20b_resolve(r, object, bar, NULL, &imp, &argc) == -1);
    CHECK(!imp);
    CHECK(ios27_objc20b_send_words(r, object, bar, NULL, NULL, 0u,
                                   &result) == -1);

    /* NULL runtime, outputs and argument vectors. */
    imp = AS_IMP(imp_seven);
    CHECK(ios27_objc20b_resolve(NULL, object, foo, NULL, &imp, &argc) == -1);
    CHECK(!imp);
    CHECK(ios27_objc20b_resolve(r, object, foo, NULL, NULL, &argc) == -1);
    CHECK(ios27_objc20b_resolve(r, object, foo, NULL, &imp, NULL) == -1);
    CHECK(ios27_objc20b_send_words(NULL, object, foo, NULL, NULL, 0u,
                                   &result) == -1);
    CHECK(ios27_objc20b_send_words(r, object, foo, NULL, NULL, 0u,
                                   NULL) == -1);
    CHECK(ios27_objc20b_send_words(r, object, foo, NULL, NULL, 1u,
                                   &result) == -1);
    CHECK(ios27_objc20b_send_words(r, object, foo, NULL, five, 5u,
                                   &result) == -1);

    /* Operations naming an unknown class, plus bad replace arguments. */
    CHECK(ios27_objc20b_alloc(r, "Nope") == NULL);
    CHECK(ios27_objc20b_alloc(r, NULL) == NULL);
    CHECK(ios27_objc20b_bind(r, foreign_object, "Nope") == -1);
    CHECK(ios27_objc20b_bind(r, NULL, "A") == -1);
    CHECK(ios27_objc20b_bind(NULL, foreign_object, "A") == -1);
    CHECK(ios27_objc20b_replace(r, "Nope", 0, &replacement) == -1);
    CHECK(ios27_objc20b_replace(NULL, "A", 0, &replacement) == -1);
    CHECK(ios27_objc20b_replace(r, "A", 2, &replacement) == -1);
    replacement.word_arguments = 5u;
    CHECK(ios27_objc20b_replace(r, "A", 0, &replacement) == -1);
    replacement.word_arguments = 0u;
    replacement.name = "";
    CHECK(ios27_objc20b_replace(r, "A", 0, &replacement) == -1);
    replacement.name = "foo";
    ios27_objc20b_statistics(r, &after);
    CHECK(b_same(&before, &after));

    /* A released foreign (bound, caller-owned) receiver is unknown again. */
    CHECK(ios27_objc20b_bind(r, foreign_object, "A") == 0);
    CHECK(ios27_objc20b_send_words(r, foreign_object, foo, NULL, NULL, 0u,
                                   &result) == 0 && result == 7u);
    CHECK(ios27_objc20b_release(r, foreign_object) == 0);
    imp = AS_IMP(imp_seven);
    CHECK(ios27_objc20b_resolve(r, foreign_object, foo, NULL, &imp,
                                &argc) == -1);
    CHECK(!imp);
    CHECK(ios27_objc20b_send_words(r, foreign_object, foo, NULL, NULL, 0u,
                                   &result) == -1);
    ios27_objc20b_statistics(r, &after);
    CHECK(b_same(&before, &after));

    /* An owned object is freed on release; its stale address must not
     * resolve. Only the address value is reused, never dereferenced. */
    stale = (uintptr_t)object;
    CHECK(ios27_objc20b_release(r, object) == 0);
    imp = AS_IMP(imp_seven);
    CHECK(ios27_objc20b_resolve(r, (void *)stale, foo, NULL, &imp,
                                &argc) == -1);
    CHECK(!imp);
    CHECK(ios27_objc20b_send_words(r, (void *)stale, foo, NULL, NULL, 0u,
                                   &result) == -1);
    ios27_objc20b_statistics(r, &after);
    CHECK(after.objects + 1u == before.objects &&
          after.epoch == before.epoch && after.classes == before.classes &&
          after.selectors == before.selectors);
    ios27_objc20b_destroy(r);
}

static void test_objc20b_caps(void) {
    const ios27_objc20b_method s0_method[1] = {
        {selector_names[0], AS_IMP(imp_seven), 0u}};
    const ios27_objc20b_method new_method[1] = {
        {selector_names[IOS27_OBJC20B_SELECTORS], AS_IMP(imp_seven), 0u}};
    ios27_objc20b_method replacement = {
        selector_names[IOS27_OBJC20B_SELECTORS], AS_IMP(imp_seven), 0u};
    ios27_objc20b_class pair[2];
    ios27_objc20b_runtime *r = NULL;
    ios27_objc20b_stats before, after;
    ios27_objc20b_sel first = NULL, again = NULL, sel = NULL;
    unsigned i;

    for (i = 0; i <= IOS27_OBJC20B_CLASSES; ++i) {
        snprintf(class_names[i], sizeof(class_names[i]), "K%u", i);
        class_specs[i] = b_spec(i, class_names[i], NULL, 0u);
    }
    for (i = 0; i <= IOS27_OBJC20B_SELECTORS; ++i)
        snprintf(selector_names[i], sizeof(selector_names[i]), "s%u", i);

    /* Class table: one past in a single batch, exactly full, one more. */
    if (ios27_objc20b_create(&r) != 0 || !r) {
        setup_failed("objc20b class-cap runtime allocation");
        return;
    }
    ios27_objc20b_statistics(r, &before);
    CHECK(ios27_objc20b_register(r, class_specs,
                                 IOS27_OBJC20B_CLASSES + 1u) == -1);
    ios27_objc20b_statistics(r, &after);
    CHECK(b_same(&before, &after));
    CHECK(ios27_objc20b_register(r, class_specs, IOS27_OBJC20B_CLASSES) == 0);
    ios27_objc20b_statistics(r, &before);
    CHECK(before.classes == IOS27_OBJC20B_CLASSES);
    CHECK(ios27_objc20b_register(r, &class_specs[IOS27_OBJC20B_CLASSES],
                                 1u) == -1);
    CHECK(ios27_objc20b_alloc(r, class_names[IOS27_OBJC20B_CLASSES]) == NULL);
    ios27_objc20b_statistics(r, &after);
    CHECK(b_same(&before, &after));
    ios27_objc20b_destroy(r);
    r = NULL;

    /* Selector table: exactly full, re-intern at capacity, one past. */
    if (ios27_objc20b_create(&r) != 0 || !r) {
        setup_failed("objc20b selector-cap runtime allocation");
        return;
    }
    for (i = 0; i < IOS27_OBJC20B_SELECTORS; ++i)
        CHECK(ios27_objc20b_selector(r, selector_names[i], &sel) == 0);
    CHECK(ios27_objc20b_selector(r, selector_names[0], &first) == 0);
    ios27_objc20b_statistics(r, &before);
    CHECK(before.selectors == IOS27_OBJC20B_SELECTORS);
    CHECK(ios27_objc20b_selector(r, selector_names[IOS27_OBJC20B_SELECTORS],
                                 &sel) == -1);
    CHECK(ios27_objc20b_selector(r, selector_names[0], &again) == 0 &&
          again == first);

    /* A batch whose second class needs a new selector fails as a whole. */
    pair[0] = b_spec(0u, "X", s0_method, 1u);
    pair[1] = b_spec(1u, "Y", new_method, 1u);
    CHECK(ios27_objc20b_register(r, pair, 2u) == -1);
    ios27_objc20b_statistics(r, &after);
    CHECK(b_same(&before, &after));
    CHECK(ios27_objc20b_register(r, pair, 1u) == 0);

    /* replace cannot intern a new selector into a full table. */
    ios27_objc20b_statistics(r, &before);
    CHECK(ios27_objc20b_replace(r, "X", 0, &replacement) == -1);
    CHECK(ios27_objc20b_replace(r, "X", 1, &replacement) == -1);
    ios27_objc20b_statistics(r, &after);
    CHECK(b_same(&before, &after));
    ios27_objc20b_destroy(r);
}

/* ---- objc20c lifetime ---- */

static int c_same(const ios27_objc20c_stats *x, const ios27_objc20c_stats *y) {
    return memcmp(x, y, sizeof(*x)) == 0;
}

static void test_objc20c(void) {
    ios27_objc20c_runtime *rt = NULL;
    ios27_objc20c_stats before, after, zero;
    void *a = NULL, *b = NULL, *dead = NULL;
    void *slot = NULL, *weak = NULL, *got = NULL;
    char *interior;
    uint32_t count = 0u;
    uint64_t token = 0u, inner = 0u;

    CHECK(ios27_objc20c_create(NULL) == -1);
    if (ios27_objc20c_create(&rt) != 0 || !rt ||
        ios27_objc20c_alloc(rt, 32u, NULL, NULL, &a) != 0 ||
        ios27_objc20c_alloc(rt, 32u, NULL, NULL, &b) != 0 ||
        ios27_objc20c_alloc(rt, 32u, NULL, NULL, &dead) != 0 ||
        ios27_objc20c_release(rt, dead) != 0) {
        setup_failed("objc20c runtime or object allocation");
        ios27_objc20c_destroy(rt);
        return;
    }
    /* dead is tombstoned: its address stays reserved until destroy. */
    interior = (char *)a + sizeof(void *);
    ios27_objc20c_get_stats(rt, &before);

    /* NULL runtime, output and location arguments. */
    memset(&zero, 0, sizeof(zero));
    memset(&after, 0xff, sizeof(after));
    ios27_objc20c_get_stats(NULL, &after);
    CHECK(c_same(&zero, &after));
    CHECK(ios27_objc20c_validate(NULL) == -1);
    got = a;
    CHECK(ios27_objc20c_alloc(NULL, 32u, NULL, NULL, &got) == -1 && !got);
    CHECK(ios27_objc20c_alloc(rt, 32u, NULL, NULL, NULL) == -1);
    CHECK(ios27_objc20c_retain(NULL, a) == -1);
    CHECK(ios27_objc20c_release(NULL, a) == -1);
    CHECK(ios27_objc20c_retain_count(NULL, a, &count) == -1);
    CHECK(ios27_objc20c_retain_count(rt, a, NULL) == -1);
    CHECK(ios27_objc20c_strong_store(NULL, &slot, a) == -1 && !slot);
    CHECK(ios27_objc20c_strong_store(rt, NULL, a) == -1);
    CHECK(ios27_objc20c_weak_store(NULL, &weak, a) == -1 && !weak);
    CHECK(ios27_objc20c_weak_store(rt, NULL, a) == -1);
    CHECK(ios27_objc20c_weak_unregister(NULL, &weak) == -1);
    CHECK(ios27_objc20c_weak_unregister(rt, NULL) == -1);
    got = a;
    CHECK(ios27_objc20c_weak_load_retained(NULL, &weak, &got) == -1 && !got);
    CHECK(ios27_objc20c_weak_load_retained(rt, &weak, NULL) == -1);
    got = a;
    CHECK(ios27_objc20c_weak_load_retained(rt, &weak, &got) == -1 && !got);
    CHECK(ios27_objc20c_pool_push(NULL, &token) == -1);
    CHECK(ios27_objc20c_pool_push(rt, NULL) == -1);
    CHECK(ios27_objc20c_autorelease(NULL, a) == -1);
    CHECK(ios27_objc20c_pool_pop(NULL, 1u) == -1);
    ios27_objc20c_get_stats(rt, &after);
    CHECK(c_same(&before, &after));

    /* Interior pointers are not objects. */
    CHECK(ios27_objc20c_retain(rt, interior) == -1);
    CHECK(ios27_objc20c_release(rt, interior) == -1);
    CHECK(ios27_objc20c_retain_count(rt, interior, &count) == -1);
    CHECK(ios27_objc20c_strong_store(rt, &slot, interior) == -1 && !slot);
    CHECK(ios27_objc20c_weak_store(rt, &weak, interior) == -1 && !weak);

    /* Dead objects in either strong_store position. */
    CHECK(ios27_objc20c_strong_store(rt, &slot, dead) == -1 && !slot);
    slot = dead;
    CHECK(ios27_objc20c_strong_store(rt, &slot, b) == -1 && slot == dead);
    CHECK(ios27_objc20c_strong_store(rt, &slot, dead) == -1 && slot == dead);
    slot = NULL;
    CHECK(ios27_objc20c_retain_count(rt, a, &count) == 0 && count == 1u);
    CHECK(ios27_objc20c_retain_count(rt, b, &count) == 0 && count == 1u);
    ios27_objc20c_get_stats(rt, &after);
    CHECK(c_same(&before, &after));

    /* Autorelease of dead and interior pointers, and pool tokens the
     * runtime never issued. */
    CHECK(ios27_objc20c_pool_push(rt, &token) == 0);
    CHECK(ios27_objc20c_pool_push(rt, &inner) == 0);
    ios27_objc20c_get_stats(rt, &before);
    CHECK(ios27_objc20c_autorelease(rt, dead) == -1);
    CHECK(ios27_objc20c_autorelease(rt, interior) == -1);
    CHECK(ios27_objc20c_pool_pop(rt, 0u) == -1);
    CHECK(ios27_objc20c_pool_pop(rt, UINT64_MAX) == -1);
    CHECK(ios27_objc20c_pool_pop(rt, inner + 1u) == -1);
    ios27_objc20c_get_stats(rt, &after);
    CHECK(c_same(&before, &after));
    CHECK(ios27_objc20c_pool_pop(rt, inner) == 0);
    CHECK(ios27_objc20c_pool_pop(rt, token) == 0);

    CHECK(ios27_objc20c_validate(rt) == 0);
    CHECK(ios27_objc20c_release(rt, a) == 0);
    CHECK(ios27_objc20c_release(rt, b) == 0);
    CHECK(ios27_objc20c_validate(rt) == 0);
    ios27_objc20c_destroy(rt);
}

int main(void) {
    test_objc20a();
    test_objc20b();
    test_objc20b_caps();
    test_objc20c();
    if (failures) {
        fprintf(stderr, "objc20-adversarial: %d check(s) failed\n", failures);
        return 1;
    }
    puts("objc20-adversarial: all checks passed");
    return 0;
}
