#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21e_tls.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"key race:%d %s\n",__LINE__,#x); abort(); } } while(0)
#define CYCLES 4000u
#define READERS 12u
static ios27_21e_runtime *runtime;
static ios27_21e_handle handles[CYCLES];
static atomic_uint published, stopped, operations, invalid;
static atomic_uint observed[CYCLES];
static int shared_value;
static void *reader(void *unused) {
    (void)unused;
    unsigned seen=0;
    while (!atomic_load_explicit(&stopped,memory_order_acquire)) {
        unsigned count=atomic_load_explicit(&published,memory_order_acquire);
        if (count==0 || count==seen) { sched_yield(); continue; }
        seen=count;
        ios27_21e_handle key=handles[count-1u]; /* Immutable after publish. */
        int rc=ios27_21e_setspecific(runtime,key,&shared_value);
        CHECK(rc==0 || rc==EINVAL);
        if (rc==EINVAL) atomic_fetch_add(&invalid,1);
        void *out=(void *)1;
        rc=ios27_21e_getspecific(runtime,key,&out);
        CHECK(rc==0 || rc==EINVAL);
        if (rc==0) CHECK(out==&shared_value || out==NULL);
        atomic_fetch_add(&operations,1);
        atomic_store_explicit(&observed[count-1u], 1u, memory_order_release);
    }
    return NULL; /* Contexts created by successful stores retire on exit. */
}
int main(void) {
    CHECK(ios27_21e_create(&runtime)==0);
    pthread_t readers[READERS];
    for(unsigned i=0;i<READERS;i++) CHECK(pthread_create(&readers[i],NULL,reader,NULL)==0);
    for(unsigned i=0;i<CYCLES;i++) {
        CHECK(ios27_21e_key_create(runtime,NULL,&handles[i])==0);
        atomic_store_explicit(&published,i+1u,memory_order_release);
        unsigned tries=0;
        while (!atomic_load_explicit(&observed[i], memory_order_acquire)) {
            CHECK(++tries < 2000000u); /* Bounded cooperative progression. */
            sched_yield();
        }
        CHECK(ios27_21e_key_delete(runtime,handles[i])==0);
        void *out=NULL;
        CHECK(ios27_21e_getspecific(runtime,handles[i],&out)==EINVAL);
    }
    atomic_store_explicit(&stopped,1u,memory_order_release);
    for(unsigned i=0;i<READERS;i++) CHECK(pthread_join(readers[i],NULL)==0);
    size_t attached=3,keys=3,tokens=3;
    CHECK(ios27_21e_stats(runtime,&attached,&keys,&tokens,NULL,NULL)==0);
    CHECK(attached==0 && keys==0 && tokens==0);
    CHECK(atomic_load(&operations)>=CYCLES);
    CHECK(ios27_21e_destroy(runtime)==0);
    printf("phase21e key deletion race: %u generations, %u concurrent operations, %u stale rejects PASS\n",
           CYCLES, atomic_load(&operations),atomic_load(&invalid));
    return 0;
}
