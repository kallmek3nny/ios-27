#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21e_tls.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"exit token:%d %s\n",__LINE__,#x);abort(); } } while(0)
#define ROUNDS 256u
static ios27_21e_runtime *runtime;
static ios27_21e_handle key;
static ios27_21e_handle old_token;
static atomic_uint entered, release_destructor, destructor_calls;
static void blocking_destructor(void *arg) {
    CHECK(arg==&runtime);
    atomic_store_explicit(&entered,1u,memory_order_release);
    unsigned tries=0;
    while(!atomic_load_explicit(&release_destructor,memory_order_acquire)) {
        CHECK(++tries<2000000u);
        sched_yield();
    }
    atomic_fetch_add(&destructor_calls,1u);
}
static void *exiting_worker(void *arg) {
    (void)arg;
    ios27_21e_handle token;
    CHECK(ios27_21e_cancel_register(runtime,&token)==0);
    old_token=token;
    CHECK(ios27_21e_setspecific(runtime,key,&runtime)==0);
    return NULL; /* Native key destructor retires token BEFORE callback. */
}
int main(void) {
    CHECK(ios27_21e_create(&runtime)==0);
    CHECK(ios27_21e_key_create(runtime,blocking_destructor,&key)==0);
    for(unsigned i=0;i<ROUNDS;i++) {
        atomic_store(&entered,0u);
        atomic_store(&release_destructor,0u);
        pthread_t t;
        CHECK(pthread_create(&t,NULL,exiting_worker,NULL)==0);
        unsigned spins=0;
        while(!atomic_load_explicit(&entered,memory_order_acquire)) {
            CHECK(++spins<2000000u);
            sched_yield();
        }
        ios27_21e_handle expired=old_token;
        CHECK(ios27_21e_cancel_request(runtime,expired)==EINVAL);
        size_t attached=0,tokens=9;
        CHECK(ios27_21e_stats(runtime,&attached,NULL,&tokens,NULL,NULL)==0);
        CHECK(attached==1u && tokens==0u);
        ios27_21e_handle replacement;
        CHECK(ios27_21e_cancel_register(runtime,&replacement)==0);
        CHECK(replacement.slot==expired.slot && replacement.generation!=expired.generation);
        CHECK(ios27_21e_cancel_request(runtime,expired)==EINVAL);
        CHECK(ios27_21e_cancel_request(runtime,replacement)==0);
        CHECK(ios27_21e_cancel_checkpoint(runtime)==ECANCELED);
        atomic_store_explicit(&release_destructor,1u,memory_order_release);
        CHECK(pthread_join(t,NULL)==0);
        /* The completed worker must never retire a recycled token. */
        CHECK(ios27_21e_cancel_checkpoint(runtime)==ECANCELED);
        CHECK(ios27_21e_cancel_request(runtime,replacement)==0);
        CHECK(ios27_21e_cancel_unregister(runtime,replacement)==0);
        CHECK(ios27_21e_thread_detach(runtime)==0);
    }
    CHECK(atomic_load(&destructor_calls)==ROUNDS);
    size_t attached=1,tokens=1;
    CHECK(ios27_21e_stats(runtime,&attached,NULL,&tokens,NULL,NULL)==0);
    CHECK(attached==0u && tokens==0u);
    CHECK(ios27_21e_key_delete(runtime,key)==0);
    CHECK(ios27_21e_destroy(runtime)==0);
    printf("phase21e exit cancellation boundary: %u in-flight destructor/reused token races PASS\n",ROUNDS);
    return 0;
}
