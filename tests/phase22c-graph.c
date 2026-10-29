#include "ios27/phase22c_graph.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BYTES 24576u
#define TEXT 512u
#define DATA 4096u
#define FIXUPS 288u
#define EXPORTS 384u

static void w16(uint8_t *p, uint16_t x) {
    p[0] = (uint8_t)x;
    p[1] = (uint8_t)(x >> 8u);
}
static void w32(uint8_t *p, uint32_t x) {
    for (unsigned i = 0u; i < 4u; ++i) p[i] = (uint8_t)(x >> (8u * i));
}
static void w64(uint8_t *p, uint64_t x) {
    w32(p, (uint32_t)x);
    w32(p + 4u, (uint32_t)(x >> 32u));
}
static void header(uint8_t *p, unsigned n, unsigned size, unsigned type) {
    w32(p, 0xfeedfacfu);
    w32(p + 4u, 0x0100000cu);
    w32(p + 12u, type);
    w32(p + 16u, n);
    w32(p + 20u, size);
}
static void segment(uint8_t *p, const char *name, uint64_t vm,
                    uint64_t sz, uint64_t off, uint64_t file,
                    uint32_t prot) {
    w32(p, 0x19u);
    w32(p + 4u, 72u);
    memcpy(p + 8u, name, strlen(name));
    w64(p + 24u, vm);
    w64(p + 32u, sz);
    w64(p + 40u, off);
    w64(p + 48u, file);
    w32(p + 56u, prot);
    w32(p + 60u, prot);
}
static void dylib(uint8_t *p, const char *name) {
    w32(p, 0x0cu);
    w32(p + 4u, 40u);
    w32(p + 8u, 24u);
    memcpy(p + 24u, name, strlen(name) + 1u);
}
static size_t trie(uint8_t *p, const char *name) {
    const size_t n = strlen(name);
    const size_t terminal = n + 4u;
    assert(n < 20u);
    p[0] = 0u;
    p[1] = 1u;
    memcpy(p + 2u, name, n + 1u);
    p[n + 3u] = (uint8_t)terminal;
    p[terminal] = 3u;
    p[terminal + 1u] = 0u;
    p[terminal + 2u] = 0x80u;
    p[terminal + 3u] = 0x04u;
    p[terminal + 4u] = 0u;
    return n + 9u;
}
static void provider(uint8_t p[BYTES], const char *dep) {
    memset(p, 0, BYTES);
    header(p, dep ? 3u : 2u, dep ? 128u : 88u, 6u);
    segment(p + 32u, "__TEXT", UINT64_C(0x100000000),
            4096u, 0u, 1024u, 5u);
    w32(p + 104u, 0x80000033u);
    w32(p + 108u, 16u);
    w32(p + 112u, EXPORTS);
    w32(p + 116u, (uint32_t)trie(p + EXPORTS, "_answer"));
    if (dep) dylib(p + 120u, dep);
    w32(p + TEXT, 0x52800540u);
    w32(p + TEXT + 4u, 0xd65f03c0u);
}
static void consumer(uint8_t p[BYTES]) {
    uint8_t *x;
    memset(p, 0, BYTES);
    header(p, 6u, 240u, 2u);
    segment(p + 32u, "__TEXT", UINT64_C(0x100000000),
            4096u, 0u, 1024u, 5u);
    segment(p + 104u, "__DATA", UINT64_C(0x100001000),
            4096u, DATA, 4096u, 3u);
    w32(p + 176u, 0x80000034u);
    w32(p + 180u, 16u);
    w32(p + 184u, FIXUPS);
    w32(p + 188u, 80u);
    dylib(p + 192u, "provider.dylib");
    w32(p + 232u, 0x80000033u);
    w32(p + 236u, 16u);
    w32(p + 240u, EXPORTS);
    w32(p + 244u, (uint32_t)trie(p + EXPORTS, "_call"));
    w32(p + 248u, 0x80000028u);
    w32(p + 252u, 24u);
    w64(p + 256u, TEXT);
    x = p + FIXUPS;
    w32(x + 4u, 28u);
    w32(x + 8u, 64u);
    w32(x + 12u, 68u);
    w32(x + 16u, 1u);
    w32(x + 20u, 1u);
    w32(x + 28u, 2u);
    w32(x + 36u, 12u);
    w32(x + 40u, 24u);
    w16(x + 44u, 4096u);
    w16(x + 46u, 6u);
    w64(x + 48u, 4096u);
    w16(x + 60u, 1u);
    w32(x + 64u, 1u);
    memcpy(x + 68u, "_answer", 8u);
    w64(p + DATA, UINT64_C(0x8010000000000000));
    w64(p + DATA + 8u, 0x70u);
    w32(p + TEXT, 0xb0000010u);
    w32(p + TEXT + 4u, 0xf9400210u);
    w32(p + TEXT + 8u, 0xd61f0200u);
}
typedef struct {
    size_t trace[64], n;
    size_t fail_at;
} callback_state;
static int init(void *opaque, size_t index) {
    callback_state *s = opaque;
    if (index == s->fail_at) return -1;
    assert(s->n < 64u);
    s->trace[s->n++] = index;
    return 0;
}
static void fini(void *opaque, size_t index) {
    callback_state *s = opaque;
    assert(s->n < 64u);
    s->trace[s->n++] = index + 100u;
}
static void check_fixture(uint8_t *main_bytes, uint8_t *lib_bytes,
                          unsigned stress) {
    const char *deps[] = {"provider.dylib"};
    ios27_image_spec specs[] = {
        {"main.exe", main_bytes, BYTES, deps, 1u},
        {"provider.dylib", lib_bytes, BYTES, NULL, 0u}
    };
    ios27_22c_graph *g = NULL;
    ios27_22c_diagnostic d;
    ios27_22c_lease token;
    ios27_export_resolution resolution;
    callback_state cb = {{0}, 0u, SIZE_MAX};
    const char *name = NULL;
    uintptr_t answer = 0u;
    assert(ios27_22c_create(specs, 2u, &g, &d) == 0);
    assert(g && d.reason == IOS27_22C_OK);
    assert(!ios27_22c_order(g, 0u, 0, &name));
    assert(!strcmp(name, "provider.dylib"));
    assert(!ios27_22c_order(g, 0u, 1, &name));
    assert(!strcmp(name, "main.exe"));
    assert(!ios27_22c_resolve(g, 0u, 1, "_answer", &resolution, &d));
    assert(resolution.library_index == 1u);
    assert(!ios27_22c_resolve(g, 0u, -2, "_answer", &resolution, &d));
    assert(ios27_22c_resolve(g, 0u, -3, "_absent", &resolution, &d) == 1);
    assert(ios27_22c_resolve(g, 0u, 9, "_answer", &resolution, &d) == -1);
    assert(d.reason == IOS27_22C_DEPENDENCY);
    assert(!ios27_22c_load(g, &d));
    assert(!ios27_22c_acquire(g, 0u, &token, &d));
    assert(ios27_22c_pins(g, 0u) == 1u);
    assert(ios27_22c_pins(g, 1u) == 1u);
    assert(!ios27_22c_symbol(g, token, 1u, "_answer", &answer));
    assert(answer != 0u);
#if defined(__aarch64__) && defined(__linux__)
    {
        int (*fn)(void);
        memcpy(&fn, &answer, sizeof(fn));
        assert(fn() == 42); /* actual ARM64 instruction execution */
    }
#endif
    assert(ios27_22c_unload(g, &d) == -1 && d.reason == IOS27_22C_BUSY);
    assert(!ios27_22c_release(g, token));
    assert(ios27_22c_release(g, token) == -1);
    assert(ios27_22c_symbol(g, token, 1u, "_answer", &answer) == -1);
    assert(!ios27_22c_initialize(g, init, fini, &cb, &d));
    assert(cb.n == 2u && cb.trace[0] == 1u && cb.trace[1] == 0u);
    assert(!ios27_22c_unload(g, &d));
    assert(cb.n == 4u && cb.trace[2] == 100u && cb.trace[3] == 101u);
    {
        callback_state failed = {{0}, 0u, 0u};
        assert(!ios27_22c_load(g, &d));
        assert(ios27_22c_initialize(g, init, fini, &failed, &d) == -1);
        assert(d.reason == IOS27_22C_LIFECYCLE);
        assert(failed.n == 2u && failed.trace[0] == 1u &&
               failed.trace[1] == 101u);
        assert(!ios27_22c_unload(g, &d));
    }
    if (stress) {
        ios27_22c_lease slots[IOS27_22C_MAX_LEASES];
        assert(!ios27_22c_load(g, &d));
        for (size_t i = 0u; i < IOS27_22C_MAX_LEASES; ++i)
            assert(!ios27_22c_acquire(g, 0u, &slots[i], &d));
        assert(ios27_22c_acquire(g, 0u, &token, &d) == -1);
        assert(token.serial == 0u && d.reason == IOS27_22C_BUSY);
        assert(ios27_22c_pins(g, 1u) == IOS27_22C_MAX_LEASES);
        for (size_t i = 0u; i < IOS27_22C_MAX_LEASES; ++i)
            assert(!ios27_22c_release(g, slots[i]));
        for (size_t k = 0; k < 50000u; ++k) {
            assert(!ios27_22c_acquire(g, 0u, &token, &d));
            assert(!ios27_22c_release(g, token));
        }
        assert(!ios27_22c_unload(g, &d));
    }
    ios27_22c_destroy(g);
}
int main(void) {
    static uint8_t a[BYTES], b[BYTES], mutate[BYTES];
    ios27_22c_diagnostic d;
    ios27_22c_graph *g = NULL;
    const char *deps[] = {"provider.dylib"};
    const char *back[] = {"main.exe"};
    ios27_image_spec specs[] = {
        {"main.exe", a, BYTES, deps, 1u},
        {"provider.dylib", b, BYTES, NULL, 0u}
    };
    provider(b, NULL);
    consumer(a);
    check_fixture(a, b, 1u);
    /* Invalid and cyclic catalogs are rejected before any mapping. */
    specs[1].name = "main.exe";
    assert(ios27_22c_create(specs, 2u, &g, &d) == -1 && !g);
    assert(d.reason == IOS27_22C_INPUT);
    specs[1].name = "provider.dylib";
    provider(b, "main.exe");
    specs[1].dependencies = back;
    specs[1].dependency_count = 1u;
    assert(ios27_22c_create(specs, 2u, &g, &d) == -1 && !g);
    assert(d.reason == IOS27_22C_CYCLE);
    provider(b, NULL);
    specs[1].dependencies = NULL;
    specs[1].dependency_count = 0u;
    memcpy(mutate, a, BYTES);
    specs[0].file = mutate;
    specs[0].size = 16u;
    assert(ios27_22c_create(specs, 2u, &g, &d) == -1 && !g);
    specs[0].size = BYTES;
    mutate[0] = 0u;
    assert(ios27_22c_create(specs, 2u, &g, &d) == -1 && !g);
    assert(d.reason == IOS27_22C_MACHO);
    specs[0].file = a;
    /* An invalid fixup import rejects the entire load; inputs are untouched. */
    memcpy(mutate, a, BYTES);
    w32(mutate + FIXUPS + 64u, 0x102u);
    specs[0].file = mutate;
    assert(!ios27_22c_create(specs, 2u, &g, &d));
    assert(ios27_22c_load(g, &d) == -1);
    assert(d.reason == IOS27_22C_FIXUP);
    ios27_22c_destroy(g);
    assert(!memcmp(mutate + FIXUPS + 64u, "\x02\x01\x00\x00", 4u));
    /* Mutate linked-format load commands and exercise full preflight
     * and mapping rollback on caller-owned copies with a deterministic seed. */
    {
        uint32_t seed = UINT32_C(0x22c15eed);
        specs[0].file = mutate;
        for (size_t iteration = 0u; iteration < 2500u; ++iteration) {
            ios27_22c_graph *trial = NULL;
            memcpy(mutate, a, BYTES);
            seed ^= seed << 13u;
            seed ^= seed >> 17u;
            seed ^= seed << 5u;
            size_t offset = (size_t)seed % 448u;
            mutate[offset] ^= (uint8_t)(1u << (iteration % 8u));
            if (!ios27_22c_create(specs, 2u, &trial, &d)) {
                if (!ios27_22c_load(trial, &d))
                    assert(!ios27_22c_unload(trial, &d));
                ios27_22c_destroy(trial);
            }
        }
    }
    printf("phase22c: 50000 pins, 2500 malformed graph loads and rollback passed\n");
    return 0;
}
