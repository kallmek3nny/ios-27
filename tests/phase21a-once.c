#define _XOPEN_SOURCE 700
#include "ios27/libsystem21a_abi.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BASE UINT64_C(0x400000000)
static unsigned char memory[8192];
static ios27_21a_runtime *rt;
static unsigned initialized, attempts, nested;
static uint64_t once_addr = BASE + 64u;
static uint64_t callback_pc = BASE + 1024u;
static int invoke(uint64_t addr, uint64_t pc) {
    uint64_t regs[6] = {addr, pc, 0, 0, 0, 0};
    uint64_t ret = UINT64_MAX;
    int e = ios27_21a_call(rt, "_pthread_once", regs, &ret);
    assert(ret == (uint64_t)e);
    return e;
}
static int initialize(void *context) {
    unsigned *value = context;
    __atomic_add_fetch(value, 1u, __ATOMIC_SEQ_CST);
    return 0;
}
static int flaky(void *context) {
    unsigned *value = context;
    unsigned now = __atomic_add_fetch(value, 1u, __ATOMIC_SEQ_CST);
    return now == 1u ? EAGAIN : 0;
}
static int recursive(void *context) {
    int e;
    (void)context;
    e = invoke(BASE + 256u, BASE + 1056u);
    assert(e == EDEADLK);
    ++nested;
    return 0;
}
static void *worker(void *unused) {
    unsigned i;
    (void)unused;
    for (i = 0; i < 1500; ++i)
        assert(invoke(once_addr, callback_pc) == 0);
    return NULL;
}
static void write_sig(size_t offset) {
    uint64_t sig = IOS27_21A_ONCE_STATIC_SIG;
    memcpy(memory + offset, &sig, sizeof(sig));
}
int main(void) {
    pthread_t threads[8];
    int i;
    assert(ios27_21a_create(&rt, memory, sizeof(memory), BASE) == 0);
    write_sig(64u);
    write_sig(128u);
    write_sig(256u);
    assert(invoke(BASE + 64u, BASE + 1024u) == ENOTSUP);
    assert(ios27_21a_register_once(rt, BASE + 1024u, initialize, &initialized) == 0);
    assert(ios27_21a_register_once(rt, BASE + 1024u, initialize, &initialized) == EEXIST);
    assert(ios27_21a_register_once(rt, BASE + 1025u, initialize, &initialized) == EINVAL);
    assert(ios27_21a_register_once(rt, BASE + 1032u, NULL, NULL) == EINVAL);
    assert(ios27_21a_register_once(rt, BASE + 99999u, initialize, NULL) == EFAULT);
    assert(ios27_21a_register_once(rt, BASE + 1032u, flaky, &attempts) == 0);
    assert(ios27_21a_register_once(rt, BASE + 1056u, recursive, NULL) == 0);
    assert(invoke(BASE + 128u, BASE + 1032u) == EAGAIN);
    assert(invoke(BASE + 128u, BASE + 1032u) == 0);
    assert(invoke(BASE + 128u, BASE + 1032u) == 0);
    assert(attempts == 2u);
    assert(invoke(BASE + 256u, BASE + 1056u) == 0);
    assert(nested == 1u);
    for (i = 0; i < 8; ++i)
        assert(pthread_create(&threads[i], NULL, worker, NULL) == 0);
    for (i = 0; i < 8; ++i)
        assert(pthread_join(threads[i], NULL) == 0);
    assert(initialized == 1u);
    assert(invoke(BASE + 64u, BASE + 1056u) == 0);
    assert(initialized == 1u);
    assert(ios27_21a_destroy(rt) == EBUSY);
    assert(ios27_21a_release_once(rt, BASE + 64u) == 0);
    assert(ios27_21a_release_once(rt, BASE + 64u) == EINVAL);
    assert(ios27_21a_release_once(rt, BASE + 128u) == 0);
    assert(ios27_21a_release_once(rt, BASE + 256u) == 0);
    assert(ios27_21a_destroy(rt) == 0);
    puts("phase21a: trusted once dispatch, 12000 races, retry and recursive init passed");
    return 0;
}
