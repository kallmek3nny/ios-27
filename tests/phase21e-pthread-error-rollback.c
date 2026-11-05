#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21e_tls.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Link with GNU -Wl,--wrap to inject ONLY ABI-boundary errors from calls
 * emitted by our compiled source; libc's own internal symbols are untouched. */
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"pthread faults:%d %s\n",__LINE__,#x);abort(); } } while(0)
static atomic_int fail_mutex,fail_key,fail_tls;
static atomic_uint rejected, forwarded;
int __real_pthread_mutex_init(pthread_mutex_t *,const pthread_mutexattr_t *);
int __real_pthread_key_create(pthread_key_t *,void (*)(void *));
int __real_pthread_setspecific(pthread_key_t,const void *);
int __wrap_pthread_mutex_init(pthread_mutex_t *m,const pthread_mutexattr_t *a) {
    if(atomic_exchange(&fail_mutex,0)) {atomic_fetch_add(&rejected,1);return EAGAIN;}
    atomic_fetch_add(&forwarded,1);
    return __real_pthread_mutex_init(m,a);
}
int __wrap_pthread_key_create(pthread_key_t *k,void (*d)(void *)) {
    if(atomic_exchange(&fail_key,0)) {atomic_fetch_add(&rejected,1);return EAGAIN;}
    atomic_fetch_add(&forwarded,1);
    return __real_pthread_key_create(k,d);
}
int __wrap_pthread_setspecific(pthread_key_t k,const void *v) {
    if(atomic_exchange(&fail_tls,0)) {atomic_fetch_add(&rejected,1);return EAGAIN;}
    atomic_fetch_add(&forwarded,1);
    return __real_pthread_setspecific(k,v);
}
static void noop(void *p) {(void)p;}
static void assert_empty(ios27_21e_runtime *r,size_t expected_keys) {
    size_t attached=9,keys=9,tokens=9;
    CHECK(ios27_21e_stats(r,&attached,&keys,&tokens,NULL,NULL)==0);
    CHECK(attached==0 && keys==expected_keys && tokens==0);
}
int main(void) {
    ios27_21e_runtime *r=(void *)(uintptr_t)1;
    atomic_store(&fail_mutex,1);
    CHECK(ios27_21e_create(&r)==EAGAIN && r==NULL);
    atomic_store(&fail_key,1);
    CHECK(ios27_21e_create(&r)==EAGAIN && r==NULL);
    CHECK(ios27_21e_create(&r)==0);
    ios27_21e_handle key;
    CHECK(ios27_21e_key_create(r,noop,&key)==0);
    atomic_store(&fail_tls,1);
    CHECK(ios27_21e_attach(r)==EAGAIN);
    assert_empty(r,1);
    atomic_store(&fail_tls,1);
    CHECK(ios27_21e_setspecific(r,key,&r)==EAGAIN);
    assert_empty(r,1);
    ios27_21e_handle token;
    atomic_store(&fail_tls,1);
    CHECK(ios27_21e_cancel_register(r,&token)==EAGAIN);
    assert_empty(r,1);
    uint64_t scope=0;
    atomic_store(&fail_tls,1);
    CHECK(ios27_21e_cleanup_push(r,noop,&r,&scope)==EAGAIN);
    assert_empty(r,1);
    CHECK(ios27_21e_attach(r)==0);
    CHECK(ios27_21e_setspecific(r,key,&r)==0);
    CHECK(ios27_21e_cancel_register(r,&token)==0);
    CHECK(ios27_21e_cleanup_push(r,noop,&r,&scope)==0);
    CHECK(ios27_21e_cleanup_pop(r,scope,1)==0);
    CHECK(ios27_21e_cancel_unregister(r,token)==0);
    CHECK(ios27_21e_thread_detach(r)==0);
    assert_empty(r,1);
    CHECK(ios27_21e_key_delete(r,key)==0);
    CHECK(ios27_21e_destroy(r)==0);
    CHECK(atomic_load(&rejected)==6u);
    CHECK(atomic_load(&forwarded)>=6u);
    puts("phase21e pthread error rollback: mutex, TLS-key, and four attach failures cleanly recovered");
    return 0;
}
