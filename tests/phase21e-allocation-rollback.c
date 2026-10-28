#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21e_tls.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Build *only this fixture* with -Dcalloc=ios27_21e_test_calloc.
 * It fail-closes precisely the implementation's direct calloc calls without
 * injecting global faults into unrelated glibc/pthread internal allocations. */
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"rollback:%d %s\n",__LINE__,#x); abort(); } } while(0)
static atomic_int inject_failure;
static atomic_uint allocations, rejected;
void *ios27_21e_test_calloc(size_t count, size_t size) {
    if (atomic_exchange(&inject_failure,0)) {
        atomic_fetch_add(&rejected,1);
        return NULL;
    }
    if (size && count > SIZE_MAX/size) return NULL;
    void *p=malloc(count*size);
    if (p) memset(p,0,count*size);
    atomic_fetch_add(&allocations,1);
    return p;
}
static void fail_next(void) { atomic_store(&inject_failure,1); }
static void noop(void *p) { (void)p; }
int main(void) {
    ios27_21e_runtime *runtime = (void *)(uintptr_t)1;
    fail_next();
    CHECK(ios27_21e_create(&runtime)==ENOMEM);
    CHECK(runtime==NULL);
    CHECK(ios27_21e_create(&runtime)==0);
    ios27_21e_handle key,token;
    CHECK(ios27_21e_key_create(runtime,noop,&key)==0);
    fail_next();
    CHECK(ios27_21e_attach(runtime)==ENOMEM);
    fail_next();
    CHECK(ios27_21e_setspecific(runtime,key,&runtime)==ENOMEM);
    fail_next();
    CHECK(ios27_21e_cancel_register(runtime,&token)==ENOMEM);
    uint64_t scope=0;
    fail_next();
    CHECK(ios27_21e_cleanup_push(runtime,noop,&runtime,&scope)==ENOMEM);
    size_t attached=9,keys=9,tokens=9;
    CHECK(ios27_21e_stats(runtime,&attached,&keys,&tokens,NULL,NULL)==0);
    CHECK(attached==0 && keys==1 && tokens==0);
    CHECK(ios27_21e_destroy(runtime)==EBUSY);
    CHECK(ios27_21e_attach(runtime)==0);
    CHECK(ios27_21e_setspecific(runtime,key,&runtime)==0);
    void *out=NULL;
    CHECK(ios27_21e_getspecific(runtime,key,&out)==0 && out==&runtime);
    CHECK(ios27_21e_cancel_register(runtime,&token)==0);
    CHECK(ios27_21e_cancel_checkpoint(runtime)==0);
    CHECK(ios27_21e_cleanup_push(runtime,noop,&runtime,&scope)==0);
    CHECK(ios27_21e_cleanup_pop(runtime,scope,0)==0);
    CHECK(ios27_21e_cancel_unregister(runtime,token)==0);
    CHECK(ios27_21e_thread_detach(runtime)==0);
    CHECK(ios27_21e_key_delete(runtime,key)==0);
    CHECK(ios27_21e_destroy(runtime)==0);
    CHECK(atomic_load(&rejected)==5);
    CHECK(atomic_load(&allocations)==2);
    puts("phase21e allocation rollback: create, attach, TLS set, cancel, cleanup ENOMEM paths safe");
    return 0;
}
