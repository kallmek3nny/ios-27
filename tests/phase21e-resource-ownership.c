#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21e_tls.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"resource ownership:%d %s\n",__LINE__,#x); abort(); } } while(0)
#define WORKERS 16u
#define ROUNDS 64u
struct record { unsigned ordinal; unsigned order; ios27_21e_handle token; };
struct resource { struct record *owner; unsigned kind; uint32_t serial; };
static ios27_21e_runtime *runtime;
static ios27_21e_handle keys[2];
static struct record records[WORKERS];
static atomic_uint ready, go, allocated, retired, cleanups, destructors;
static struct resource *take(struct record *r, unsigned kind) {
    struct resource *v=malloc(sizeof(*v));
    CHECK(v!=NULL);
    v->owner=r;
    v->kind=kind;
    v->serial=(uint32_t)(r->ordinal*7u+kind);
    atomic_fetch_add(&allocated,1u);
    return v;
}
static void release_resource(struct resource *v, unsigned kind) {
    CHECK(v!=NULL && v->kind==kind);
    CHECK(v->serial==v->owner->ordinal*7u+v->kind);
    CHECK(v->owner->order==321u);
    v->serial=0u;
    free(v);
    atomic_fetch_add(&retired,1u);
}
static void clean_resource(void *pointer) {
    struct resource *v=pointer;
    CHECK(v && v->kind>=1u && v->kind<=3u);
    CHECK(v->owner->order<1000u);
    v->owner->order=v->owner->order*10u+v->kind;
    /* Cleanup handlers own their resource before the next handler starts. */
    free(v);
    atomic_fetch_add(&retired,1u);
    atomic_fetch_add(&cleanups,1u);
}
static void free_tls0(void *pointer) {
    struct resource *v=pointer;
    CHECK(v->owner->order==321u);
    CHECK(ios27_21e_key_delete(runtime,keys[0])==EBUSY);
    release_resource(v,4u);
    atomic_fetch_add(&destructors,1u);
}
static void free_tls1(void *pointer) {
    struct resource *v=pointer;
    CHECK(v->owner->order==321u);
    CHECK(ios27_21e_key_delete(runtime,keys[1])==EBUSY);
    release_resource(v,5u);
    atomic_fetch_add(&destructors,1u);
}
static void *worker(void *pointer) {
    struct record *r=pointer;
    uint64_t scopes[3];
    CHECK(ios27_21e_cancel_register(runtime,&r->token)==0);
    CHECK(ios27_21e_cancel_setstate(runtime,IOS27_21E_CANCEL_DISABLE,NULL)==0);
    CHECK(ios27_21e_setspecific(runtime,keys[0],take(r,4u))==0);
    CHECK(ios27_21e_setspecific(runtime,keys[1],take(r,5u))==0);
    for(unsigned i=0;i<3u;i++) {
        CHECK(ios27_21e_cleanup_push(runtime,clean_resource,take(r,i+1u),&scopes[i])==0);
        if(i) CHECK(scopes[i]>scopes[i-1u]);
    }
    atomic_fetch_add_explicit(&ready,1u,memory_order_release);
    while(!atomic_load_explicit(&go,memory_order_acquire)) sched_yield();
    CHECK(ios27_21e_cancel_checkpoint(runtime)==0);
    CHECK(ios27_21e_cancel_setstate(runtime,IOS27_21E_CANCEL_ENABLE,NULL)==0);
    CHECK(ios27_21e_cancel_checkpoint(runtime)==ECANCELED);
    if(r->ordinal&1u) {
        CHECK(ios27_21e_cleanup_unwind(runtime)==0);
        CHECK(ios27_21e_cancel_unregister(runtime,r->token)==0);
        CHECK(ios27_21e_thread_detach(runtime)==0);
    }
    return NULL; /* Even workers use native automatic exit cleanup. */
}
int main(void) {
    CHECK(ios27_21e_create(&runtime)==0);
    CHECK(ios27_21e_key_create(runtime,free_tls0,&keys[0])==0);
    CHECK(ios27_21e_key_create(runtime,free_tls1,&keys[1])==0);
    for(unsigned batch=0;batch<ROUNDS;batch++) {
        pthread_t threads[WORKERS];
        atomic_store(&ready,0u); atomic_store(&go,0u);
        for(unsigned i=0;i<WORKERS;i++) {
            records[i].ordinal=i;
            records[i].order=0u;
            CHECK(pthread_create(&threads[i],NULL,worker,&records[i])==0);
        }
        unsigned spins=0;
        while(atomic_load_explicit(&ready,memory_order_acquire)!=WORKERS) {
            CHECK(++spins<2000000u); sched_yield();
        }
        for(unsigned i=0;i<WORKERS;i++)
            CHECK(ios27_21e_cancel_request(runtime,records[i].token)==0);
        atomic_store_explicit(&go,1u,memory_order_release);
        for(unsigned i=0;i<WORKERS;i++) {
            CHECK(pthread_join(threads[i],NULL)==0);
            CHECK(records[i].order==321u);
            CHECK(ios27_21e_cancel_request(runtime,records[i].token)==EINVAL);
        }
        size_t attached=9,live_keys=9,tokens=9;
        CHECK(ios27_21e_stats(runtime,&attached,&live_keys,&tokens,NULL,NULL)==0);
        CHECK(attached==0 && live_keys==2 && tokens==0);
    }
    CHECK(atomic_load(&allocated)==WORKERS*ROUNDS*5u);
    CHECK(atomic_load(&retired)==atomic_load(&allocated));
    CHECK(atomic_load(&cleanups)==WORKERS*ROUNDS*3u);
    CHECK(atomic_load(&destructors)==WORKERS*ROUNDS*2u);
    /* Replacing a TLS value and deleting its key do not transfer ownership
     * to the runtime's destructor. The original owner reclaims both values. */
    struct record local={.ordinal=20u,.order=321u};
    struct resource *replaced=take(&local,4u);
    struct resource *orphaned=take(&local,4u);
    CHECK(ios27_21e_setspecific(runtime,keys[0],replaced)==0);
    CHECK(ios27_21e_setspecific(runtime,keys[0],orphaned)==0);
    release_resource(replaced,4u);
    CHECK(ios27_21e_key_delete(runtime,keys[0])==0);
    release_resource(orphaned,4u);
    void *out=NULL;
    CHECK(ios27_21e_getspecific(runtime,keys[0],&out)==EINVAL);
    CHECK(ios27_21e_thread_detach(runtime)==0);
    CHECK(atomic_load(&allocated)==WORKERS*ROUNDS*5u+2u);
    CHECK(atomic_load(&retired)==atomic_load(&allocated));
    CHECK(atomic_load(&destructors)==WORKERS*ROUNDS*2u);
    CHECK(ios27_21e_key_delete(runtime,keys[1])==0);
    CHECK(ios27_21e_destroy(runtime)==0);
    printf("phase21e ownership: %u resources, %u LIFO cleanups, %u TLS destructors; no leaks PASS\n",
        atomic_load(&allocated),atomic_load(&cleanups),atomic_load(&destructors));
    return 0;
}
