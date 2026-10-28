#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21e_tls.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"cross runtime:%d %s\n",__LINE__,#x); abort(); } } while(0)
#define RUNTIMES 3u
#define BATCHES 48u
#define WORKERS 16u
struct value { uint32_t runtime; uint32_t worker; };
static ios27_21e_runtime *r[RUNTIMES];
static ios27_21e_handle keys[RUNTIMES];
static atomic_uint seen_cleanup[RUNTIMES], seen_destructor[RUNTIMES];
static void cleanup(void *pointer) {
    struct value *v=pointer;
    CHECK(v && v->runtime < RUNTIMES);
    CHECK(ios27_21e_cancel_checkpoint(r[v->runtime]) == 0);
    atomic_fetch_add(&seen_cleanup[v->runtime], 1);
}
static void destructor(void *pointer) {
    struct value *v=pointer;
    CHECK(v && v->runtime < RUNTIMES);
    ios27_21e_runtime *rt = r[v->runtime];
    void *out = (void *)1;
    CHECK(ios27_21e_getspecific(rt, keys[v->runtime], &out) == 0 && out == NULL);
    CHECK(ios27_21e_key_delete(rt, keys[v->runtime]) == EBUSY);
    CHECK(ios27_21e_thread_detach(rt) == EBUSY);
    atomic_fetch_add(&seen_destructor[v->runtime], 1);
    free(v);
}
static void *worker(void *pointer) {
    uint32_t serial=(uint32_t)(uintptr_t)pointer;
    for (unsigned i=0;i<RUNTIMES;i++) {
        struct value *v=malloc(sizeof(*v));
        CHECK(v != NULL);
        v->runtime=i;
        v->worker=serial;
        ios27_21e_handle token;
        uint64_t scope=0;
        CHECK(ios27_21e_cancel_register(r[i], &token) == 0);
        CHECK(ios27_21e_setspecific(r[i], keys[i], v) == 0);
        CHECK(ios27_21e_cleanup_push(r[i], cleanup, v, &scope) == 0);
        CHECK(scope != 0);
        CHECK(ios27_21e_cancel_checkpoint(r[i]) == 0);
    }
    return NULL; /* Three independent native TLS destructors on exit. */
}
int main(void) {
    for (unsigned i=0;i<RUNTIMES;i++) {
        CHECK(ios27_21e_create(&r[i]) == 0);
        CHECK(ios27_21e_key_create(r[i], destructor, &keys[i]) == 0);
    }
    for (unsigned batch=0;batch<BATCHES;batch++) {
        pthread_t threads[WORKERS];
        for (unsigned i=0;i<WORKERS;i++)
            CHECK(pthread_create(&threads[i], NULL, worker,
                  (void *)(uintptr_t)(batch*WORKERS+i)) == 0);
        for (unsigned i=0;i<WORKERS;i++) CHECK(pthread_join(threads[i], NULL) == 0);
    }
    for (unsigned i=0;i<RUNTIMES;i++) {
        size_t attached=9,keys_live=9,tokens=9;
        uint64_t cleanups=0,dtors=0;
        CHECK(ios27_21e_stats(r[i],&attached,&keys_live,&tokens,&cleanups,&dtors) == 0);
        CHECK(attached==0 && keys_live==1 && tokens==0);
        CHECK(cleanups==WORKERS*BATCHES && dtors==WORKERS*BATCHES);
        CHECK(atomic_load(&seen_cleanup[i]) == WORKERS*BATCHES);
        CHECK(atomic_load(&seen_destructor[i]) == WORKERS*BATCHES);
        CHECK(ios27_21e_key_delete(r[i],keys[i]) == 0);
        CHECK(ios27_21e_destroy(r[i]) == 0);
    }
    puts("phase21e cross-runtime teardown: 2304 cleanup + TLS destructor pairs safely retired");
    return 0;
}
