#define _XOPEN_SOURCE 700
#include "ios27/libsystem21a_abi.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BASE UINT64_C(0x1400000000)
static unsigned char memory[8192];
static ios27_21a_runtime *runtime;
static pthread_barrier_t barrier;
static uint64_t guest_threads[8], assigned[8];
static int invoke(const char *symbol, uint64_t a, uint64_t b, uint64_t *out) {
    const uint64_t registers[6] = {a, b, 0, 0, 0, 0};
    uint64_t x0 = UINT64_MAX;
    int e = ios27_21a_call(runtime, symbol, registers, &x0);
    *out = x0;
    return e;
}
static void *worker(void *arg) {
    uintptr_t index = (uintptr_t)arg;
    uint64_t ret, id;
    int e;
    assert(ios27_21a_attach_thread(runtime, guest_threads[index]) == 0);
    assert(invoke("pthread_main_np", 0u, 0u, &ret) == 0 && ret == 0u);
    assert(invoke("pthread_threadid_np", 0u, BASE + 3000u +
                  index * 16u, &ret) == 0 && ret == 0u);
    memcpy(&id, memory + 3000u + index * 16u, sizeof(id));
    assert(id > 0u);
    assigned[index] = id;
    e = pthread_barrier_wait(&barrier);
    assert(e == 0 || e == PTHREAD_BARRIER_SERIAL_THREAD);
    assert(invoke("pthread_is_threaded_np", 0u, 0u, &ret) == 0 && ret == 1u);
    assert(ios27_21a_detach_thread(runtime) == 0);
    return NULL;
}
int main(void) {
    pthread_t threads[8];
    uint64_t ret, id, former;
    unsigned i, j;
    assert(ios27_21a_create(&runtime, memory, sizeof(memory), BASE) == 0);
    assert(invoke("pthread_main_np", 0, 0, &ret) == 0 && ret == 1u);
    assert(invoke("pthread_is_threaded_np", 0, 0, &ret) == 0 && ret == 0u);
    assert(invoke("pthread_threadid_np", 0, BASE + 1024u, &ret) == ESRCH);
    assert(ret == (uint64_t)ESRCH);
    assert(ios27_21a_attach_thread(runtime, BASE + 64u) == 0);
    assert(invoke("pthread_threadid_np", 0, BASE + 1024u, &ret) == 0);
    memcpy(&id, memory + 1024u, sizeof(id));
    assert(id == 1u);
    assert(invoke("pthread_threadid_np", BASE + 64u, BASE + 1032u, &ret) == 0);
    memcpy(&former, memory + 1032u, sizeof(former));
    assert(former == id);
    assert(invoke("pthread_threadid_np", BASE + 64u, BASE + 64u, &ret) == EINVAL);
    assert(invoke("pthread_threadid_np", BASE + 64u, BASE + 1u, &ret) == EFAULT);
    assert(invoke("pthread_threadid_np", BASE + 99999u, BASE + 1040u, &ret) == ESRCH);
    for (i = 0; i < 8u; ++i)
        guest_threads[i] = BASE + 256u + (uint64_t)i * 64u;
    assert(pthread_barrier_init(&barrier, NULL, 8u) == 0);
    for (i = 0; i < 8u; ++i)
        assert(pthread_create(&threads[i], NULL, worker,
                              (void *)(uintptr_t)i) == 0);
    for (i = 0; i < 8u; ++i)
        assert(pthread_join(threads[i], NULL) == 0);
    assert(pthread_barrier_destroy(&barrier) == 0);
    for (i = 0; i < 8u; ++i) {
        assert(assigned[i] > id);
        for (j = i + 1u; j < 8u; ++j)
            assert(assigned[i] != assigned[j]);
    }
    assert(invoke("pthread_is_threaded_np", 0, 0, &ret) == 0 && ret == 1u);
    assert(invoke("pthread_threadid_np", guest_threads[0],
                  BASE + 1072u, &ret) == ESRCH);
    assert(ios27_21a_detach_thread(runtime) == 0);
    assert(ios27_21a_destroy(runtime) == 0);
    puts("phase21a: Darwin threadid, main and threaded flags with 8 unique IDs passed");
    return 0;
}
