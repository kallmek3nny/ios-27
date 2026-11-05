#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21e_tls.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"state model:%d %s\n",__LINE__,#x); abort(); } } while(0)
#define WORKERS 16u
#ifndef ACTIONS
#define ACTIONS 16000u
#endif
#ifndef MODEL_SEED
#define MODEL_SEED UINT32_C(0x9e3779b9)
#endif
#define MODEL_KEYS 8u
struct model {
    uint32_t random_state;
    unsigned expected_cleanup;
    unsigned observed_cleanup;
    unsigned depth;
    uint64_t scopes[IOS27_21E_MAX_CLEANUPS];
    uint64_t last_scope;
    ios27_21e_handle token;
    int token_active;
    int pending;
    enum ios27_21e_cancel_state state;
    void *values[MODEL_KEYS];
    uintptr_t markers[MODEL_KEYS];
};
static ios27_21e_runtime *runtime;
static ios27_21e_handle keys[MODEL_KEYS];
static struct model models[WORKERS];
static atomic_uint ready, go;
static unsigned choose(struct model *m, unsigned limit) {
    uint32_t x=m->random_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    m->random_state=x;
    return x%limit;
}
static void cleanup(void *pointer) {
    struct model *m=pointer;
    ++m->observed_cleanup;
}
static void check_value(struct model *m, unsigned i) {
    void *value=(void *)1;
    CHECK(ios27_21e_getspecific(runtime,keys[i],&value)==0);
    CHECK(value==m->values[i]);
}
static void *worker(void *pointer) {
    struct model *m=pointer;
    CHECK(ios27_21e_attach(runtime)==0);
    m->state=IOS27_21E_CANCEL_ENABLE;
    atomic_fetch_add_explicit(&ready,1u,memory_order_release);
    while(!atomic_load_explicit(&go,memory_order_acquire)) sched_yield();
    for(unsigned n=0;n<ACTIONS;n++) {
        unsigned action=choose(m,17u);
        if(action<=3u) {
            unsigned i=choose(m,MODEL_KEYS);
            void *value=(choose(m,3u)==0u) ? NULL : &m->markers[i];
            CHECK(ios27_21e_setspecific(runtime,keys[i],value)==0);
            m->values[i]=value;
            check_value(m,i);
        } else if(action==4u) {
            check_value(m,choose(m,MODEL_KEYS));
        } else if(action==5u) {
            int rc=ios27_21e_cancel_register(runtime,&m->token);
            if(m->token_active) CHECK(rc==EALREADY);
            else {
                CHECK(rc==0);
                m->token_active=1;
                m->pending=0;
                m->state=IOS27_21E_CANCEL_ENABLE;
            }
        } else if(action==6u && m->token_active) {
            CHECK(ios27_21e_cancel_request(runtime,m->token)==0);
            m->pending=1;
        } else if(action==7u && m->token_active) {
            enum ios27_21e_cancel_state next = choose(m,2u)
                ? IOS27_21E_CANCEL_ENABLE : IOS27_21E_CANCEL_DISABLE;
            enum ios27_21e_cancel_state previous=(enum ios27_21e_cancel_state)77;
            CHECK(ios27_21e_cancel_setstate(runtime,next,&previous)==0);
            CHECK(previous==m->state);
            m->state=next;
        } else if(action==8u) {
            int rc=ios27_21e_cancel_checkpoint(runtime);
            int wanted=!m->token_active ? EINVAL :
                (m->pending && m->state==IOS27_21E_CANCEL_ENABLE ? ECANCELED : 0);
            CHECK(rc==wanted);
        } else if(action==9u) {
            uint64_t scope=0;
            int rc=ios27_21e_cleanup_push(runtime,cleanup,m,&scope);
            if(m->depth==IOS27_21E_MAX_CLEANUPS) CHECK(rc==EAGAIN);
            else {
                CHECK(rc==0 && scope>m->last_scope);
                m->last_scope=scope;
                m->scopes[m->depth++]=scope;
            }
        } else if(action==10u) {
            if(m->depth) {
                int execute=(int)choose(m,2u);
                CHECK(ios27_21e_cleanup_pop(runtime,m->scopes[--m->depth],execute)==0);
                m->expected_cleanup+=(unsigned)execute;
            } else CHECK(ios27_21e_cleanup_pop(runtime,1u,1)==EINVAL);
        } else if(action==11u) {
            CHECK(ios27_21e_cleanup_unwind(runtime)==0);
            m->expected_cleanup+=m->depth;
            m->depth=0;
        } else if(action==12u) {
            CHECK(ios27_21e_thread_detach(runtime)==0);
            m->expected_cleanup+=m->depth;
            m->depth=0;
            m->token_active=0;
            m->pending=0;
            m->state=IOS27_21E_CANCEL_ENABLE;
            for(unsigned i=0;i<MODEL_KEYS;i++) m->values[i]=NULL;
            CHECK(ios27_21e_attach(runtime)==0);
            CHECK(ios27_21e_cancel_checkpoint(runtime)==EINVAL);
        } else if(action==13u) {
            CHECK(ios27_21e_cleanup_pop(runtime,m->last_scope+1u,1)==EINVAL);
        } else if(action==14u) {
            ios27_21e_handle bad=keys[choose(m,MODEL_KEYS)];
            bad.runtime_id^=UINT64_C(0x1234);
            void *value=NULL;
            CHECK(ios27_21e_getspecific(runtime,bad,&value)==EINVAL);
            CHECK(ios27_21e_setspecific(runtime,bad,&m->markers[0])==EINVAL);
        } else if(action==15u && m->token_active) {
            int rc=ios27_21e_cancel_unregister(runtime,m->token);
            if(m->depth) CHECK(rc==EBUSY);
            else { CHECK(rc==0); m->token_active=0; m->pending=0; }
        } else {
            size_t keys_live=0;
            CHECK(ios27_21e_stats(runtime,NULL,&keys_live,NULL,NULL,NULL)==0);
            CHECK(keys_live==MODEL_KEYS);
        }
        CHECK(m->observed_cleanup==m->expected_cleanup);
    }
    m->expected_cleanup+=m->depth;
    return NULL; /* Native pthread exit drains any remaining handlers. */
}
int main(void) {
    CHECK(ios27_21e_create(&runtime)==0);
    for(unsigned i=0;i<MODEL_KEYS;i++)
        CHECK(ios27_21e_key_create(runtime,NULL,&keys[i])==0);
    pthread_t threads[WORKERS];
    for(unsigned i=0;i<WORKERS;i++) {
        models[i].random_state=(uint32_t)(MODEL_SEED ^ (i+1u)*UINT32_C(0x85ebca6b));
        for(unsigned j=0;j<MODEL_KEYS;j++) models[i].markers[j]=i*MODEL_KEYS+j;
        CHECK(pthread_create(&threads[i],NULL,worker,&models[i])==0);
    }
    unsigned spins=0;
    while(atomic_load_explicit(&ready,memory_order_acquire)!=WORKERS) {
        CHECK(++spins<2000000u); sched_yield();
    }
    atomic_store_explicit(&go,1u,memory_order_release);
    unsigned cleanups=0;
    for(unsigned i=0;i<WORKERS;i++) {
        CHECK(pthread_join(threads[i],NULL)==0);
        CHECK(models[i].observed_cleanup==models[i].expected_cleanup);
        cleanups+=models[i].observed_cleanup;
    }
    size_t attached=1,keys_live=0,tokens=1;
    CHECK(ios27_21e_stats(runtime,&attached,&keys_live,&tokens,NULL,NULL)==0);
    CHECK(attached==0 && keys_live==MODEL_KEYS && tokens==0);
    for(unsigned i=0;i<MODEL_KEYS;i++) CHECK(ios27_21e_key_delete(runtime,keys[i])==0);
    CHECK(ios27_21e_destroy(runtime)==0);
    printf("phase21e state model: %u concurrent randomized operations, %u exact LIFO cleanups; PASS\n",
        WORKERS*ACTIONS,cleanups);
    return 0;
}
