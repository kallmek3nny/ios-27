/* Deterministic property tests for the bounded named guest heap
 * (malloc/calloc/realloc/free in src/darwin_shim.c).
 *
 * A shadow model of the eight heap slots and the eight model pages of the
 * shared 32 KiB mmap quota predicts every call before it runs. After each
 * step the model's page map is read back through the service table and
 * every live block must still hold its byte pattern. A rejected call must
 * set the documented errno and leave the heap unchanged; a successful call
 * must leave errno alone. Directed groups pin the edges and fixed-seed LCG
 * sequences cover the rest. A failure prints the line, phase, seed and step.
 * Page placement is not asserted: the model only requires that a request
 * succeeds exactly when a free run of enough contiguous pages exists. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ios27/darwin_shim.h"
#include "darwin_shim_internal.h"
#include "service_bridge_internal.h"

#define PAGE 4096u
#define MAP_BASE (IOS27_SERVICE_ARENA_BYTES / 2u)
#define MAP_PAGES (MAP_BASE / PAGE)
#define MAX_REQUEST ((uint64_t)MAP_BASE)
#define SLOTS 8u
#define ENOMEM_VALUE 12
#define EINVAL_VALUE 22
#define SENTINEL 37
#define STEPS 3000u
#define COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define CHECK(condition) do { if (!(condition)) fail(__LINE__, #condition); } while (0)

enum { PAGE_FREE, PAGE_HEAP, PAGE_MAP };
enum { FITS, NO_SIZE, NO_SLOT, NO_PAGES };

typedef struct {
    uint8_t *pointer;
    uint64_t size, reserved;
    uint32_t pattern;
} block;

static const ios27_service_api *service;
static uintptr_t arena_base;
static block live[SLOTS];
static unsigned live_count;
static uint8_t pages[MAP_PAGES];
static uint8_t *mapping;
static uint64_t mapping_bytes;
static uint8_t *stale;
static uint8_t static_bytes[64];
static uint64_t lcg_state, seed_now;
static uint32_t pattern_counter;
static unsigned step_now;
static const char *phase = "setup";

static struct {
    unsigned long allocated, freed, quota, slots, oversize, overflow;
    unsigned long in_place, moved, kept, released, einval, maps, map_rejects;
} seen;

static const uint64_t edge_sizes[] = {
    0u, 1u, 15u, 16u, 17u, PAGE - 1u, PAGE, PAGE + 1u, 2u * PAGE - 1u, 2u * PAGE,
    2u * PAGE + 1u, 3u * PAGE, 4u * PAGE, 4u * PAGE + 1u, MAX_REQUEST - 1u, MAX_REQUEST,
};
static const uint64_t oversizes[] = {
    MAX_REQUEST + 1u, 2u * MAX_REQUEST, UINT64_C(1) << 32, UINT64_MAX / 2u,
    UINT64_MAX - PAGE + 1u, UINT64_MAX,
};
static const uint64_t edge_counts[] = {
    0u, 1u, 2u, 3u, 8u, PAGE, PAGE + 1u, UINT64_C(1) << 32,
    (UINT64_C(1) << 52) + 1u, UINT64_MAX,
};
static const uint64_t calloc_sizes[] = {
    0u, 1u, 2u, 8u, 16u, PAGE, UINT64_C(1) << 32,
    UINT64_C(0x5555555555555556), UINT64_MAX,
};

static void fail(int line, const char *expression) {
    fprintf(stderr, "guest-heap-property FAIL line %d phase %s seed 0x%016llx step %u errno %d: %s\n",
            line, phase, (unsigned long long)seed_now, step_now, (int)*__error(), expression);
    exit(1);
}

static uint32_t roll(void) {
    lcg_state = lcg_state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return (uint32_t)(lcg_state >> 32);
}

static uint8_t pattern_byte(uint32_t pattern, uint64_t index) {
    return (uint8_t)(pattern ^ (index * 131u) ^ (index >> 9));
}

static void fill(block *b) {
    b->pattern = 0x9e3779b9u * ++pattern_counter;
    for (uint64_t i = 0; i < b->size; ++i) b->pointer[i] = pattern_byte(b->pattern, i);
}

static int holds(const block *b, uint64_t length) {
    for (uint64_t i = 0; i < length; ++i)
        if (b->pointer[i] != pattern_byte(b->pattern, i)) return 0;
    return 1;
}

/* Zero-byte requests reserve one byte; every reservation is whole pages. */
static uint64_t round_pages(uint64_t size) {
    uint64_t want = size ? size : 1u;
    return (want + PAGE - 1u) / PAGE * PAGE;
}

static int page_run(uint64_t need) {
    for (uint64_t first = 0; first + need <= MAP_PAGES; ++first) {
        uint64_t k = 0;
        while (k < need && pages[first + k] == PAGE_FREE) ++k;
        if (k == need) return 1;
    }
    return 0;
}

static int judge(uint64_t size) {
    uint64_t want = size ? size : 1u;
    if (want > MAX_REQUEST) return NO_SIZE;
    if (live_count == SLOTS) return NO_SLOT;
    return page_run(round_pages(want) / PAGE) ? FITS : NO_PAGES;
}

static unsigned find_live(const void *pointer) {
    for (unsigned i = 0; i < live_count; ++i)
        if (live[i].pointer == pointer) return i;
    return SLOTS;
}

static unsigned first_page(const void *pointer, uint64_t bytes) {
    uint64_t offset = (uint64_t)((uintptr_t)pointer - arena_base);
    CHECK(!((uintptr_t)pointer & 15u));
    CHECK(offset % PAGE == 0u);
    CHECK(offset >= MAP_BASE && offset < IOS27_SERVICE_ARENA_BYTES);
    CHECK(bytes && bytes % PAGE == 0u && bytes <= IOS27_SERVICE_ARENA_BYTES - offset);
    return (unsigned)((offset - MAP_BASE) / PAGE);
}

static void claim(const void *pointer, uint64_t bytes, uint8_t kind) {
    unsigned first = first_page(pointer, bytes);
    for (uint64_t i = 0; i < bytes / PAGE; ++i) {
        CHECK(pages[first + i] == PAGE_FREE);
        pages[first + i] = kind;
    }
}

static void release(const void *pointer, uint64_t bytes, uint8_t kind) {
    unsigned first = first_page(pointer, bytes);
    for (uint64_t i = 0; i < bytes / PAGE; ++i) {
        CHECK(pages[first + i] == kind);
        pages[first + i] = PAGE_FREE;
    }
}

/* Read the model's page map back and compare it with the shadow. */
static void verify_state(void) {
    uint64_t heap_bytes = 0, reserved = 0;
    for (unsigned i = 0; i < MAP_PAGES; ++i) {
        uint64_t offset = MAP_BASE + (uint64_t)i * PAGE;
        const void *expected = (const void *)(arena_base + (uintptr_t)offset);
        const void *readable = service->memory(offset, PAGE, 0u);
        const void *writable = service->memory(offset, PAGE, 1u);
        if (pages[i] == PAGE_FREE) {
            CHECK(readable == NULL && writable == NULL);
        } else {
            CHECK(readable == expected && writable == expected);
            if (pages[i] == PAGE_HEAP) heap_bytes += PAGE;
        }
    }
    for (unsigned i = 0; i < live_count; ++i) {
        CHECK(live[i].size && live[i].size <= live[i].reserved);
        CHECK(holds(&live[i], live[i].size));
        reserved += live[i].reserved;
    }
    CHECK(reserved == heap_bytes);
    if (mapping) CHECK(mapping[0] == 0x5a && mapping[mapping_bytes - 1u] == 0xa5);
}

static void add_block(uint8_t *pointer, uint64_t size, uint64_t zeroed) {
    block *b;
    CHECK(live_count < SLOTS);
    b = &live[live_count];
    b->pointer = pointer;
    b->size = size ? size : 1u;
    b->reserved = round_pages(b->size);
    claim(pointer, b->reserved, PAGE_HEAP);
    for (uint64_t i = 0; i < zeroed; ++i) CHECK(pointer[i] == 0);
    fill(b);
    ++live_count;
    ++seen.allocated;
}

static void drop_block(unsigned index) {
    stale = live[index].pointer;
    release(live[index].pointer, live[index].reserved, PAGE_HEAP);
    live[index] = live[--live_count];
}

static void finish_alloc(int verdict, void *result, uint64_t size, uint64_t zeroed) {
    if (verdict == FITS) {
        CHECK(result != NULL);
        CHECK(*__error() == SENTINEL);
        add_block(result, size, zeroed);
    } else {
        CHECK(result == NULL);
        CHECK(*__error() == ENOMEM_VALUE);
        if (verdict == NO_SIZE) ++seen.oversize;
        else if (verdict == NO_SLOT) ++seen.slots;
        else ++seen.quota;
    }
    verify_state();
}

static void op_malloc(uint64_t size) {
    int verdict = judge(size);
    *__error() = SENTINEL;
    finish_alloc(verdict, malloc(size), size, 0u);
}

static void op_realloc_null(uint64_t size) {
    int verdict = judge(size);
    *__error() = SENTINEL;
    finish_alloc(verdict, realloc(NULL, size), size, 0u);
}

static void op_calloc(uint64_t count, uint64_t size) {
    int overflow = count && size > UINT64_MAX / count;
    uint64_t total = overflow ? 0u : count * size;
    int verdict = overflow ? NO_SIZE : judge(total);
    void *result;
    *__error() = SENTINEL;
    result = calloc(count, size);
    if (overflow) {
        /* A product that wraps must be rejected, never allocated small. */
        CHECK(result == NULL);
        CHECK(*__error() == ENOMEM_VALUE);
        ++seen.overflow;
        verify_state();
        return;
    }
    finish_alloc(verdict, result, total, total);
}

static void op_free(unsigned index) {
    CHECK(index < live_count);
    *__error() = SENTINEL;
    free(live[index].pointer);
    CHECK(*__error() == SENTINEL);
    drop_block(index);
    ++seen.freed;
    verify_state();
}

static void op_free_null(void) {
    *__error() = SENTINEL;
    free(NULL);
    CHECK(*__error() == SENTINEL);
    verify_state();
}

static void op_realloc(unsigned index, uint64_t size) {
    block before, after;
    int verdict = FITS;
    void *result;
    CHECK(index < live_count);
    before = live[index];
    if (size > before.reserved) verdict = judge(size);
    *__error() = SENTINEL;
    result = realloc(before.pointer, size);
    if (!size) {
        /* realloc(p, 0) frees p and returns NULL without an error. */
        CHECK(result == NULL);
        CHECK(*__error() == SENTINEL);
        drop_block(index);
        ++seen.released;
    } else if (size <= before.reserved) {
        CHECK(result == (void *)before.pointer);
        CHECK(*__error() == SENTINEL);
        CHECK(holds(&before, size < before.size ? size : before.size));
        live[index].size = size;
        fill(&live[index]);
        ++seen.in_place;
    } else if (verdict != FITS) {
        /* The original stays owned; verify_state re-checks its bytes. */
        CHECK(result == NULL);
        CHECK(*__error() == ENOMEM_VALUE);
        ++seen.kept;
    } else {
        CHECK(result != NULL && result != (void *)before.pointer);
        CHECK(*__error() == SENTINEL);
        after.pointer = result;
        after.size = size;
        after.reserved = round_pages(size);
        after.pattern = before.pattern;
        CHECK(holds(&after, before.size));
        claim(after.pointer, after.reserved, PAGE_HEAP);
        release(before.pointer, before.reserved, PAGE_HEAP);
        live[index] = after;
        fill(&live[index]);
        stale = before.pointer;
        ++seen.moved;
    }
    verify_state();
}

/* free and realloc of a pointer the heap does not own must fail with
 * EINVAL and change nothing, including realloc(pointer, 0). */
static void expect_rejected(void *pointer, uint64_t size) {
    CHECK(pointer != NULL && find_live(pointer) == SLOTS);
    *__error() = SENTINEL;
    free(pointer);
    CHECK(*__error() == EINVAL_VALUE);
    verify_state();
    *__error() = SENTINEL;
    CHECK(realloc(pointer, size) == NULL);
    CHECK(*__error() == EINVAL_VALUE);
    seen.einval += 2u;
    verify_state();
}

static void op_map(uint64_t count) {
    uint64_t bytes = count * PAGE;
    int fits = page_run(count);
    void *result;
    CHECK(mapping == NULL);
    *__error() = SENTINEL;
    result = mmap(NULL, bytes, IOS27_DARWIN_PROT_READ | IOS27_DARWIN_PROT_WRITE,
                  IOS27_DARWIN_MAP_ANON | IOS27_DARWIN_MAP_PRIVATE, -1, 0);
    if (!fits) {
        CHECK(result == IOS27_DARWIN_MAP_FAILED);
        CHECK(*__error() == ENOMEM_VALUE);
        ++seen.map_rejects;
    } else {
        CHECK(result != IOS27_DARWIN_MAP_FAILED);
        CHECK(*__error() == SENTINEL);
        claim(result, bytes, PAGE_MAP);
        mapping = result;
        mapping_bytes = bytes;
        for (uint64_t i = 0; i < bytes; ++i) CHECK(mapping[i] == 0);
        mapping[0] = 0x5a;
        mapping[bytes - 1u] = 0xa5;
        ++seen.maps;
    }
    verify_state();
}

static void op_unmap(void) {
    CHECK(mapping != NULL);
    *__error() = SENTINEL;
    CHECK(munmap(mapping, mapping_bytes) == 0);
    CHECK(*__error() == SENTINEL);
    release(mapping, mapping_bytes, PAGE_MAP);
    mapping = NULL;
    mapping_bytes = 0;
    verify_state();
}

static void drain(void) {
    while (live_count) op_free(live_count - 1u);
    if (mapping) op_unmap();
    for (unsigned i = 0; i < MAP_PAGES; ++i) CHECK(pages[i] == PAGE_FREE);
    stale = NULL;
}

static uint64_t pick_size(void) {
    uint32_t kind = roll() % 8u;
    if (kind < 4u) return edge_sizes[roll() % COUNT(edge_sizes)];
    if (kind < 7u) return 1u + roll() % (3u * PAGE);
    return 1u + roll() % MAX_REQUEST;
}

static uint64_t pick_resize(void) {
    return roll() % 8u ? pick_size() : 0u;
}

static void op_calloc_random(void) {
    uint64_t count, size;
    if (roll() % 2u) {
        count = edge_counts[roll() % COUNT(edge_counts)];
        size = calloc_sizes[roll() % COUNT(calloc_sizes)];
    } else {
        count = 1u + roll() % 64u;
        size = 1u + roll() % 512u;
    }
    op_calloc(count, size);
}

static void op_reject_random(void) {
    uint8_t stack_bytes[16] = {0};
    void *candidates[6];
    unsigned count = 0;
    void *pick;
    candidates[count++] = stack_bytes;
    candidates[count++] = static_bytes + roll() % sizeof(static_bytes);
    candidates[count++] = (void *)(arena_base + 256u); /* shim scratch, lower half */
    if (mapping) candidates[count++] = mapping + (roll() % (mapping_bytes / PAGE)) * PAGE;
    if (live_count) {
        const block *b = &live[roll() % live_count];
        candidates[count++] = b->pointer + 1u + roll() % (b->reserved - 1u);
    }
    if (stale && find_live(stale) == SLOTS) candidates[count++] = stale;
    pick = candidates[roll() % count];
    expect_rejected(pick, pick_resize());
}

static void op_double_free(void) {
    if (stale && find_live(stale) == SLOTS) expect_rejected(stale, pick_resize());
    else op_free_null();
}

static void op_oversize(void) {
    uint64_t size = oversizes[roll() % COUNT(oversizes)];
    uint32_t how = roll() % 4u;
    if (how == 0u) op_malloc(size);
    else if (how == 1u) op_calloc(size, 2u);
    else if (how == 2u && live_count) op_realloc(roll() % live_count, size);
    else op_realloc_null(size);
}

static void random_step(void) {
    uint32_t kind = roll() % 100u;
    if (kind < 22u) {
        op_malloc(pick_size());
    } else if (kind < 30u) {
        op_calloc_random();
    } else if (kind < 35u) {
        op_realloc_null(pick_size());
    } else if (kind < 55u) {
        if (live_count) {
            unsigned index = roll() % live_count;
            op_realloc(index, pick_resize());
        } else {
            op_malloc(pick_size());
        }
    } else if (kind < 72u) {
        if (live_count) op_free(roll() % live_count);
        else op_free_null();
    } else if (kind < 86u) {
        op_reject_random();
    } else if (kind < 92u) {
        if (mapping) op_unmap();
        else op_map(1u + roll() % 4u);
    } else if (kind < 96u) {
        op_double_free();
    } else {
        op_oversize();
    }
}

static void directed_zero_and_overflow(void) {
    unsigned long before;
    phase = "zero-and-overflow";
    op_free_null();
    op_malloc(0u);
    CHECK(live_count == 1u && live[0].size == 1u && live[0].reserved == PAGE);
    op_realloc_null(0u);
    op_calloc(0u, 0u);
    op_calloc(0u, UINT64_MAX);
    op_calloc(UINT64_MAX, 0u);
    CHECK(live_count == 5u);
    before = seen.overflow;
    op_calloc(UINT64_C(1) << 32, UINT64_C(1) << 32);      /* wraps to 0 */
    op_calloc(UINT64_C(1) << 63, 2u);                      /* wraps to 0 */
    op_calloc(3u, UINT64_C(0x5555555555555556));           /* wraps to 2 */
    op_calloc((UINT64_C(1) << 52) + 1u, PAGE);             /* wraps to 4096 */
    op_calloc(UINT64_MAX, 2u);
    op_calloc(2u, UINT64_MAX);
    CHECK(live_count == 5u && seen.overflow - before == 6u);
    op_calloc(PAGE, 8u);                                   /* 32 KiB, five pages in use */
    CHECK(live_count == 5u && *__error() == ENOMEM_VALUE);
    drain();
    op_calloc(PAGE, 8u);                                   /* the whole quota, zeroed */
    CHECK(live_count == 1u && live[0].reserved == MAX_REQUEST);
    drain();
}

static void directed_quota(void) {
    unsigned long before;
    phase = "quota";
    for (unsigned i = 0; i < 4u; ++i) op_malloc(PAGE + 1u); /* two pages each */
    CHECK(live_count == 4u);
    before = seen.quota;
    op_malloc(1u);                       /* four slots free, no page free */
    op_calloc(1u, 1u);
    op_realloc_null(0u);
    CHECK(live_count == 4u && seen.quota - before == 3u);
    op_free(0u);
    op_malloc(2u * PAGE);                /* recovery: the freed two-page run */
    CHECK(live_count == 4u);
    drain();
    op_malloc(MAX_REQUEST);
    op_malloc(0u);
    CHECK(live_count == 1u && *__error() == ENOMEM_VALUE);
    drain();
    before = seen.oversize;
    op_malloc(MAX_REQUEST + 1u);
    op_malloc(UINT64_MAX - PAGE + 1u);   /* page rounding would wrap to 0 */
    op_realloc_null(UINT64_MAX);
    op_calloc(1u, MAX_REQUEST + 1u);
    CHECK(live_count == 0u && seen.oversize - before == 4u);
    op_malloc(PAGE);
    op_malloc(MAX_REQUEST);              /* seven pages free, eight needed */
    CHECK(live_count == 1u && *__error() == ENOMEM_VALUE);
    drain();
}

static void directed_slots_and_fragmentation(void) {
    uint8_t *by_page[MAP_PAGES] = {0};
    phase = "slots-and-fragmentation";
    for (unsigned i = 0; i < SLOTS; ++i) op_malloc(PAGE);
    CHECK(live_count == SLOTS);
    op_malloc(0u);                       /* no slot and no page left */
    CHECK(live_count == SLOTS && *__error() == ENOMEM_VALUE);
    op_realloc(0u, 2u * PAGE);           /* growth needs a new block; original kept */
    CHECK(live_count == SLOTS && *__error() == ENOMEM_VALUE);
    for (unsigned i = 0; i < live_count; ++i)
        by_page[first_page(live[i].pointer, PAGE)] = live[i].pointer;
    for (unsigned i = 0; i < MAP_PAGES; i += 2u) {
        CHECK(by_page[i] != NULL);
        op_free(find_live(by_page[i]));
    }
    CHECK(live_count == SLOTS / 2u);
    op_malloc(2u * PAGE);                /* four pages free, no two adjacent */
    CHECK(live_count == SLOTS / 2u && *__error() == ENOMEM_VALUE);
    for (unsigned i = 0; i < 4u; ++i) op_malloc(1u + i * 1000u);
    CHECK(live_count == SLOTS);
    drain();
}

static void directed_realloc(void) {
    uint8_t *original, *moved;
    phase = "realloc";
    op_malloc(2u * PAGE);
    original = live[0].pointer;
    op_realloc(0u, 1u);                  /* shrink in place */
    CHECK(live[0].pointer == original && live[0].size == 1u);
    op_realloc(0u, 2u * PAGE);           /* regrow inside the reservation */
    CHECK(live[0].pointer == original);
    op_realloc(0u, 2u * PAGE + 1u);      /* past the reservation: the block moves */
    moved = live[0].pointer;
    CHECK(moved != original && live[0].reserved == 3u * PAGE);
    expect_rejected(original, 0u);       /* stale after the move */
    expect_rejected(original, PAGE);
    while (judge(PAGE) == FITS) op_malloc(PAGE);
    CHECK(live_count == 6u);
    op_realloc(0u, 4u * PAGE);           /* no room for the new block */
    CHECK(live[0].pointer == moved && *__error() == ENOMEM_VALUE);
    op_realloc(0u, MAX_REQUEST + 1u);
    op_realloc(0u, UINT64_MAX);
    CHECK(live[0].pointer == moved && *__error() == ENOMEM_VALUE);
    op_realloc(0u, 0u);                  /* frees without an error */
    CHECK(live_count == 5u && *__error() == SENTINEL);
    expect_rejected(moved, 1u);          /* the zero-size realloc released it */
    drain();
}

static void directed_foreign_pointers(void) {
    uint8_t stack_bytes[32] = {0};
    uint8_t *base;
    phase = "foreign-pointers";
    op_malloc(2u * PAGE + 5u);
    base = live[0].pointer;
    op_map(2u);
    CHECK(mapping != NULL);
    expect_rejected(stack_bytes, 1u);
    expect_rejected(stack_bytes + 1, 0u);
    expect_rejected(static_bytes, PAGE);
    expect_rejected((void *)(arena_base + 256u), 1u);  /* shim scratch, lower half */
    expect_rejected(mapping, 1u);                      /* explicit mmap, not heap */
    expect_rejected(mapping + PAGE, 0u);
    expect_rejected(base + 1, 1u);                     /* misaligned interior */
    expect_rejected(base + 16, 0u);                    /* aligned interior */
    expect_rejected(base + PAGE, 2u * PAGE);           /* page-aligned interior */
    expect_rejected(base + 2u * PAGE + 4u, 1u);        /* last byte */
    op_free(0u);
    expect_rejected(base, 1u);                         /* double free */
    op_unmap();
    drain();
}

static void directed_shared_quota(void) {
    phase = "shared-mmap-quota";
    op_map(MAP_PAGES / 2u);
    CHECK(mapping != NULL);
    op_malloc(MAX_REQUEST / 2u + 1u);    /* five pages needed, four free */
    CHECK(live_count == 0u && *__error() == ENOMEM_VALUE);
    op_malloc(MAX_REQUEST / 2u);
    op_unmap();
    drain();
    op_malloc(MAX_REQUEST);
    CHECK(live_count == 1u);
    op_map(1u);                          /* the heap holds the whole quota */
    CHECK(mapping == NULL && *__error() == ENOMEM_VALUE);
    drain();
}

int main(void) {
    static const uint64_t seeds[] = {
        UINT64_C(1), UINT64_C(0x9e3779b97f4a7c15), UINT64_C(20261108),
        UINT64_C(0xdeadbeefcafef00d),
    };
    const void *first;
    service = ios27_service_bridge_initialize();
    CHECK(service != NULL);
    ios27_shim_binding.api = service;
    first = service->memory(1u, 0u, 0u);
    CHECK(first != NULL);
    arena_base = (uintptr_t)first - 1u;
    verify_state();

    directed_zero_and_overflow();
    directed_quota();
    directed_slots_and_fragmentation();
    directed_realloc();
    directed_foreign_pointers();
    directed_shared_quota();

    memset(&seen, 0, sizeof(seen));
    for (unsigned s = 0; s < COUNT(seeds); ++s) {
        seed_now = lcg_state = seeds[s];
        phase = "random";
        for (step_now = 0; step_now < STEPS; ++step_now) random_step();
        phase = "random-drain";
        drain();
    }
    /* Every outcome class must occur in the sweep, or it proves little. */
    CHECK(seen.allocated && seen.freed && seen.quota && seen.oversize && seen.overflow);
    CHECK(seen.in_place && seen.moved && seen.kept && seen.released);
    CHECK(seen.einval && seen.maps);

    seed_now = 0;
    step_now = 0;
    phase = "recovery";
    op_malloc(MAX_REQUEST);
    CHECK(live_count == 1u);
    drain();
    printf("guest heap property: 6 directed groups and %u seeds x %u steps passed; "
           "sweep: %lu allocations, %lu frees, %lu quota, %lu slot, %lu oversize and "
           "%lu calloc-overflow rejects, %lu in-place, %lu moved, %lu kept and "
           "%lu zero-size reallocs, %lu EINVAL rejects, %lu maps, %lu map rejects\n",
           (unsigned)COUNT(seeds), STEPS, seen.allocated, seen.freed, seen.quota, seen.slots,
           seen.oversize, seen.overflow, seen.in_place, seen.moved, seen.kept, seen.released,
           seen.einval, seen.maps, seen.map_rejects);
    return 0;
}
