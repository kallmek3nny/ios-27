#include "ios27/phase25c_lifetime.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct { void *isa; void *child; } node;
static int isa_identity;
static const ios27_phase25c_class klass = {
    &isa_identity, 27, sizeof(node), _Alignof(node),
    offsetof(node, child), sizeof(void *)
};
static void *expected;
static unsigned consumed, finalized;
extern void phase25c_compiler_arc(void *);

void phase25c_consume(void *object) {
    assert(object == expected);
    ++consumed;
}
static void dealloc(ios27_phase25c_runtime *rt, void *object, void *ctx) {
    (void)rt;
    (void)ctx;
    assert(object == expected);
    ++finalized;
}
int main(void) {
    ios27_phase25c_runtime *rt = NULL;
    assert(ios27_phase25c_create(&rt) == 0);
    assert(ios27_phase25c_alloc(rt, &klass, dealloc, NULL, &expected) == 0);
    ios27_phase25c_runtime *previous = ios27_phase25c_scope_enter(rt);
    for (unsigned i = 0; i < 256; ++i) phase25c_compiler_arc(expected);
    ios27_phase25c_scope_leave(previous);
    assert(ios27_phase25c_thread_detach(rt) == 0);
    assert(consumed == 512);
    uint32_t refs = 0;
    assert(ios27_phase25c_retain_count(rt, expected, &refs) == 0 && refs == 1);
    ios27_phase25c_stats s;
    ios27_phase25c_get_stats(rt, &s);
    assert(s.weak_loads >= 256 && s.weak_slots == 0);
    assert(s.transitions >= 512);
    assert(ios27_phase25c_release(rt, expected) == 0);
    assert(finalized == 1 && ios27_phase25c_validate(rt) == 0);
    assert(ios27_phase25c_destroy(&rt) == 0 && rt == NULL);
    puts("phase25c: 256 genuine compiler-generated native ARC executions passed");
    return 0;
}
