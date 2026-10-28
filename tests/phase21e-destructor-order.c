#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21e_tls.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr,"destructor order %d: %s\n",__LINE__,#e);abort();}} while(0)
#define WORKERS 12
#define BATCHES 64
static ios27_21e_runtime *r;
static ios27_21e_handle first, second;
static atomic_uint second_calls, first_calls, freed;
typedef struct { unsigned id; unsigned nested; } allocation;
static allocation *alloc(unsigned id, unsigned nested) {
    allocation *p = malloc(sizeof(*p));
    CHECK(p != NULL);
    p->id=id; p->nested=nested;
    return p;
}
static void first_dtor(void *v) {
    allocation *p=v;
    CHECK(p->nested == 1);
    void *cleared = (void *)1;
    CHECK(ios27_21e_getspecific(r, first, &cleared)==0 && cleared==NULL);
    atomic_fetch_add(&first_calls,1);
    free(p);
    atomic_fetch_add(&freed,1);
}
static void second_dtor(void *v) {
    allocation *p=v;
    CHECK(p->nested == 0);
    atomic_fetch_add(&second_calls,1);
    /* first key was visited earlier in this destructor pass. Its newly
       allocated value must be handled on the next pass. */
    CHECK(ios27_21e_setspecific(r, first, alloc(p->id, 1))==0);
    free(p);
    atomic_fetch_add(&freed,1);
}
static void *worker(void *arg) {
    unsigned id = (unsigned)(uintptr_t)arg;
    CHECK(ios27_21e_setspecific(r, second, alloc(id, 0)) == 0);
    return NULL;
}
int main(void) {
    CHECK(ios27_21e_create(&r)==0);
    CHECK(ios27_21e_key_create(r,first_dtor,&first)==0);
    CHECK(ios27_21e_key_create(r,second_dtor,&second)==0);
    CHECK(first.slot < second.slot);
    for(unsigned b=0;b<BATCHES;++b) {
        pthread_t threads[WORKERS];
        for(unsigned i=0;i<WORKERS;++i)
            CHECK(pthread_create(&threads[i],NULL,worker,
                (void *)(uintptr_t)(b*WORKERS+i))==0);
        for(unsigned i=0;i<WORKERS;++i) CHECK(pthread_join(threads[i],NULL)==0);
    }
    CHECK(atomic_load(&second_calls)==WORKERS*BATCHES);
    CHECK(atomic_load(&first_calls)==WORKERS*BATCHES);
    CHECK(atomic_load(&freed)==2u*WORKERS*BATCHES);
    CHECK(ios27_21e_key_delete(r,second)==0);
    CHECK(ios27_21e_key_delete(r,first)==0);
    CHECK(ios27_21e_destroy(r)==0);
    puts("phase21e destructor ordering: 768 nested cross-key TLS allocations safely released");
    return 0;
}
