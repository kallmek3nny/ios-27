#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21e_tls.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do {if (!(x)) {fprintf(stderr,"isolation:%d: %s\n",__LINE__,#x);abort();}} while(0)
static atomic_uint destructor_calls;
static void cleanup_noop(void *value) { CHECK(value!=NULL); }
static void destructor(void *value) {
    CHECK(value != NULL);
    atomic_fetch_add(&destructor_calls, 1);
}
static void inspect_two_runtimes(void) {
    ios27_21e_runtime *a = NULL, *b = NULL;
    ios27_21e_handle ak, bk, at, bt;
    CHECK(ios27_21e_create(&a) == 0);
    CHECK(ios27_21e_create(&b) == 0);
    CHECK(ios27_21e_key_create(a, destructor, &ak) == 0);
    CHECK(ios27_21e_key_create(b, destructor, &bk) == 0);
    CHECK(ak.slot == bk.slot && ak.generation == bk.generation);
    CHECK(ak.runtime_id != bk.runtime_id && ak.runtime_id != 0 && bk.runtime_id != 0);
    void *value = NULL;
    CHECK(ios27_21e_setspecific(a, bk, &a) == EINVAL);
    CHECK(ios27_21e_getspecific(b, ak, &value) == EINVAL);
    CHECK(ios27_21e_key_delete(a, bk) == EINVAL);
    CHECK(ios27_21e_setspecific(a, ak, &a) == 0);
    CHECK(ios27_21e_setspecific(b, bk, &b) == 0);
    CHECK(ios27_21e_cancel_register(a, &at) == 0);
    CHECK(ios27_21e_cancel_register(b, &bt) == 0);
    CHECK(at.slot == bt.slot && at.generation == bt.generation);
    CHECK(at.runtime_id != bt.runtime_id);
    CHECK(ios27_21e_cancel_request(a, bt) == EINVAL);
    CHECK(ios27_21e_cancel_request(b, at) == EINVAL);
    CHECK(ios27_21e_cancel_request(a, at) == 0);
    CHECK(ios27_21e_cancel_checkpoint(a) == ECANCELED);
    CHECK(ios27_21e_cancel_checkpoint(b) == 0);
    uint64_t ascope=0, bscope=0;
    CHECK(ios27_21e_cleanup_push(a,cleanup_noop,&a,&ascope)==0);
    CHECK(ios27_21e_cleanup_push(b,cleanup_noop,&b,&bscope)==0);
    CHECK(ascope!=bscope && ascope!=0 && bscope!=0);
    CHECK(ios27_21e_cleanup_pop(a,bscope,1)==EINVAL);
    CHECK(ios27_21e_cleanup_pop(b,ascope,1)==EINVAL);
    CHECK(ios27_21e_cleanup_pop(a,ascope,1)==0);
    CHECK(ios27_21e_cleanup_pop(b,bscope,1)==0);
    CHECK(ios27_21e_cancel_unregister(a, at) == 0);
    CHECK(ios27_21e_cancel_unregister(b, bt) == 0);
    CHECK(ios27_21e_thread_detach(a) == 0);
    CHECK(ios27_21e_cleanup_push(a,cleanup_noop,&a,&ascope)==0);
    CHECK(ios27_21e_cleanup_pop(a,bscope,1)==EINVAL);
    CHECK(ios27_21e_cleanup_pop(a,ascope,1)==0);
    CHECK(ios27_21e_thread_detach(a) == 0);
    CHECK(ios27_21e_thread_detach(b) == 0);
    CHECK(atomic_load(&destructor_calls) == 2);
    CHECK(ios27_21e_key_delete(a, ak) == 0);
    CHECK(ios27_21e_key_delete(b, bk) == 0);
    CHECK(ios27_21e_destroy(a) == 0);
    CHECK(ios27_21e_destroy(b) == 0);
}
#define CREATORS 24u
#define CREATIONS_PER_THREAD 256u
static uint64_t runtime_ids[CREATORS*CREATIONS_PER_THREAD];
static void *concurrent_creator(void *arg) {
    unsigned id=(unsigned)(uintptr_t)arg;
    for(unsigned i=0;i<CREATIONS_PER_THREAD;i++) {
        ios27_21e_runtime *rt=NULL;
        ios27_21e_handle key={0};
        CHECK(ios27_21e_create(&rt)==0);
        CHECK(ios27_21e_key_create(rt,NULL,&key)==0);
        runtime_ids[id*CREATIONS_PER_THREAD+i]=key.runtime_id;
        CHECK(ios27_21e_key_delete(rt,key)==0);
        CHECK(ios27_21e_destroy(rt)==0);
    }
    return NULL;
}
static int compare_ids(const void *a, const void *b) {
    uint64_t aa=*(const uint64_t *)a, bb=*(const uint64_t *)b;
    return (aa>bb)-(aa<bb);
}
static void inspect_concurrent_creation(void) {
    pthread_t threads[CREATORS];
    for(unsigned i=0;i<CREATORS;i++)
        CHECK(pthread_create(&threads[i],NULL,concurrent_creator,(void *)(uintptr_t)i)==0);
    for(unsigned i=0;i<CREATORS;i++) CHECK(pthread_join(threads[i],NULL)==0);
    qsort(runtime_ids,CREATORS*CREATIONS_PER_THREAD,sizeof(runtime_ids[0]),compare_ids);
    CHECK(runtime_ids[0]!=0);
    for(unsigned i=1;i<CREATORS*CREATIONS_PER_THREAD;i++)
        CHECK(runtime_ids[i]!=runtime_ids[i-1u]);
}
static void inspect_sequential_recreation(void) {
    ios27_21e_handle stale = {0}, newer = {0};
    for (unsigned i = 0; i < 1000; ++i) {
        ios27_21e_runtime *r;
        CHECK(ios27_21e_create(&r) == 0);
        CHECK(ios27_21e_key_create(r, NULL, &newer) == 0);
        if (i) CHECK(newer.runtime_id != stale.runtime_id);
        stale = newer;
        CHECK(ios27_21e_key_delete(r, newer) == 0);
        CHECK(ios27_21e_destroy(r) == 0);
    }
}
int main(void) {
    inspect_two_runtimes();
    inspect_sequential_recreation();
    inspect_concurrent_creation();
    puts("phase21e runtime isolation: cross-runtime alias rejection and 7144 distinct runtime lifetimes safe");
    return 0;
}
