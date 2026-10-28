#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21e_tls.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"cancel race:%d %s\n",__LINE__,#x); abort(); } } while(0)
#define WORKERS 16u
#define REQUESTERS 6u
#define ROUNDS 80u
#define REQUESTS_PER_REQUESTER 1500u
struct record { ios27_21e_handle token; unsigned ordinal; };
static ios27_21e_runtime *runtime;
static struct record records[WORKERS];
static atomic_uint ready, go, requesters_ready, accepted, stale, checked, cleanups;
static void cleanup(void *arg) {
    struct record *r=arg;
    CHECK(r->ordinal < WORKERS);
    atomic_fetch_add_explicit(&cleanups,1u,memory_order_relaxed);
}
static void *worker(void *arg) {
    struct record *r=arg;
    ios27_21e_handle token;
    CHECK(ios27_21e_cancel_register(runtime,&token)==0);
    CHECK(ios27_21e_cancel_setstate(runtime,IOS27_21E_CANCEL_DISABLE,NULL)==0);
    r->token=token;
    atomic_fetch_add_explicit(&ready,1u,memory_order_release);
    while(!atomic_load_explicit(&go,memory_order_acquire)) sched_yield();
    CHECK(ios27_21e_cancel_checkpoint(runtime)==0);
    enum ios27_21e_cancel_state previous;
    CHECK(ios27_21e_cancel_setstate(runtime,IOS27_21E_CANCEL_ENABLE,&previous)==0);
    CHECK(previous==IOS27_21E_CANCEL_DISABLE);
    CHECK(ios27_21e_cancel_checkpoint(runtime)==ECANCELED);
    uint64_t scope=0;
    CHECK(ios27_21e_cleanup_push(runtime,cleanup,r,&scope)==0);
    CHECK(ios27_21e_cancel_unregister(runtime,token)==EBUSY);
    CHECK(ios27_21e_cleanup_pop(runtime,scope,1)==0);
    CHECK(ios27_21e_cancel_unregister(runtime,token)==0);
    CHECK(ios27_21e_cancel_checkpoint(runtime)==EINVAL);
    if((r->ordinal&1u)==0) CHECK(ios27_21e_thread_detach(runtime)==0);
    atomic_fetch_add_explicit(&checked,1u,memory_order_release);
    return NULL;
}
static void *requester(void *arg) {
    unsigned offset=(unsigned)(uintptr_t)arg;
    atomic_fetch_add_explicit(&requesters_ready,1u,memory_order_release);
    for (unsigned n=0;n<REQUESTS_PER_REQUESTER;n++) {
        const struct record *r=&records[(n+offset)%WORKERS];
        int rc=ios27_21e_cancel_request(runtime,r->token);
        CHECK(rc==0 || rc==EINVAL);
        if(rc==0) atomic_fetch_add_explicit(&accepted,1u,memory_order_relaxed);
        else atomic_fetch_add_explicit(&stale,1u,memory_order_relaxed);
        if((n&31u)==0) sched_yield();
    }
    return NULL;
}
int main(void) {
    CHECK(ios27_21e_create(&runtime)==0);
    ios27_21e_handle old_tokens[WORKERS]={0};
    for(unsigned batch=0;batch<ROUNDS;batch++) {
        pthread_t workers[WORKERS], callers[REQUESTERS];
        atomic_store(&ready,0u); atomic_store(&go,0u);
        atomic_store(&requesters_ready,0u);
        for(unsigned i=0;i<WORKERS;i++) {
            records[i].ordinal=i;
            CHECK(pthread_create(&workers[i],NULL,worker,&records[i])==0);
        }
        unsigned spins=0;
        while(atomic_load_explicit(&ready,memory_order_acquire)!=WORKERS) {
            CHECK(++spins<2000000u); sched_yield();
        }
        if(batch>0) {
            for(unsigned i=0;i<WORKERS;i++)
                CHECK(ios27_21e_cancel_request(runtime,old_tokens[i])==EINVAL);
        }
        for(unsigned i=0;i<WORKERS;i++) {
            CHECK(ios27_21e_cancel_request(runtime,records[i].token)==0);
            old_tokens[i]=records[i].token;
        }
        for(unsigned i=0;i<REQUESTERS;i++)
            CHECK(pthread_create(&callers[i],NULL,requester,(void *)(uintptr_t)i)==0);
        spins=0;
        while(atomic_load_explicit(&requesters_ready,memory_order_acquire)!=REQUESTERS) {
            CHECK(++spins<2000000u); sched_yield();
        }
        atomic_store_explicit(&go,1u,memory_order_release);
        for(unsigned i=0;i<WORKERS;i++) CHECK(pthread_join(workers[i],NULL)==0);
        for(unsigned i=0;i<REQUESTERS;i++) CHECK(pthread_join(callers[i],NULL)==0);
        for(unsigned i=0;i<WORKERS;i++)
            CHECK(ios27_21e_cancel_request(runtime,old_tokens[i])==EINVAL);
        size_t attached=1,keys=1,tokens=1;
        CHECK(ios27_21e_stats(runtime,&attached,&keys,&tokens,NULL,NULL)==0);
        CHECK(attached==0 && keys==0 && tokens==0);
    }
    CHECK(atomic_load(&cleanups)==WORKERS*ROUNDS);
    CHECK(atomic_load(&checked)==WORKERS*ROUNDS);
    CHECK(atomic_load(&accepted)>=WORKERS*ROUNDS);
    CHECK(ios27_21e_destroy(runtime)==0);
    printf("phase21e cancellation races: %u workers, %u requests accepted, %u stale requests rejected; PASS\n",
            WORKERS*ROUNDS,atomic_load(&accepted),atomic_load(&stale));
    return 0;
}
