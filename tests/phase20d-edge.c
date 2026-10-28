#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem20d_threads.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define VERIFY(x) do { if (!(x)) { \
 fprintf(stderr,"edge:%d failed: %s\n",__LINE__,#x); exit(1); } \
} while(0)
static ios27_20d_runtime *rt;
static ios27_20d_handle mutex, cond;
static atomic_int ready, released;
static int predicate;
static void tiny_pause(void) {
    struct timespec ts = {0, 1000000L};
    (void)nanosleep(&ts, NULL);
}
static void wait_count(atomic_int *var, int value) {
    for (int i=0; i<4000 && atomic_load(var)!=value; ++i) tiny_pause();
    VERIFY(atomic_load(var)==value);
}
static void *waiter(void *ignored) {
    (void)ignored;
    VERIFY(ios27_20d_mutex_lock(rt,mutex)==0);
    atomic_fetch_add(&ready,1);
    while (!predicate) VERIFY(ios27_20d_cond_wait(rt,cond,mutex,0)==0);
    VERIFY(ios27_20d_mutex_unlock(rt,mutex)==0);
    atomic_fetch_add(&released,1);
    return NULL;
}
static void test_wait_and_broadcast(void) {
    ios27_20d_handle workers[6];
    VERIFY(ios27_20d_create(&rt,6)==0);
    VERIFY(ios27_20d_sync_create(rt, IOS27_20D_MUTEX, &mutex)==0);
    VERIFY(ios27_20d_sync_create(rt, IOS27_20D_CONDITION, &cond)==0);
    atomic_store(&ready,0);
    atomic_store(&released,0);
    predicate=0;
    for(int i=0;i<6;++i)
        VERIFY(ios27_20d_thread_create(rt,&workers[i],waiter,NULL)==0);
    wait_count(&ready,6);
    VERIFY(ios27_20d_sync_destroy(rt,cond)==EBUSY);
    VERIFY(ios27_20d_mutex_lock(rt,mutex)==0);
    predicate=1;
    VERIFY(ios27_20d_cond_signal(rt,cond,1)==0);
    VERIFY(ios27_20d_mutex_unlock(rt,mutex)==0);
    for(int i=0;i<6;++i) VERIFY(ios27_20d_thread_join(rt,workers[i],NULL)==0);
    VERIFY(atomic_load(&released)==6);
    VERIFY(ios27_20d_sync_destroy(rt,cond)==0);
    VERIFY(ios27_20d_sync_destroy(rt,mutex)==0);
    VERIFY(ios27_20d_destroy(rt)==0);
}
static void test_quotas_generations(void) {
    ios27_20d_handle handles[IOS27_20D_MAX_SYNCS], keys[IOS27_20D_MAX_KEYS];
    ios27_20d_handle old, replacement;
    void *value=NULL;
    VERIFY(ios27_20d_create(&rt,1)==0);
    for(size_t i=0;i<IOS27_20D_MAX_SYNCS;++i)
        VERIFY(ios27_20d_sync_create(rt, IOS27_20D_MUTEX, &handles[i])==0);
    VERIFY(ios27_20d_sync_create(rt,IOS27_20D_MUTEX,&replacement)==EAGAIN);
    old=handles[0];
    VERIFY(ios27_20d_sync_destroy(rt,old)==0);
    VERIFY(ios27_20d_sync_create(rt,IOS27_20D_MUTEX,&replacement)==0);
    VERIFY(replacement.slot==old.slot && replacement.generation!=old.generation);
    VERIFY(ios27_20d_mutex_lock(rt,old)==EINVAL);
    VERIFY(ios27_20d_sync_destroy(rt,old)==EINVAL);
    VERIFY(ios27_20d_sync_destroy(rt,replacement)==0);
    for(size_t i=1;i<IOS27_20D_MAX_SYNCS;++i)
        VERIFY(ios27_20d_sync_destroy(rt,handles[i])==0);
    for(size_t i=0;i<IOS27_20D_MAX_KEYS;++i)
        VERIFY(ios27_20d_key_create(rt,&keys[i],NULL)==0);
    VERIFY(ios27_20d_key_create(rt,&replacement,NULL)==EAGAIN);
    old=keys[0];
    VERIFY(ios27_20d_key_delete(rt,old)==0);
    VERIFY(ios27_20d_key_create(rt,&replacement,NULL)==0);
    VERIFY(replacement.slot==old.slot && replacement.generation!=old.generation);
    VERIFY(ios27_20d_getspecific(rt,old,&value)==EINVAL);
    VERIFY(ios27_20d_key_delete(rt,replacement)==0);
    for(size_t i=1;i<IOS27_20D_MAX_KEYS;++i)
        VERIFY(ios27_20d_key_delete(rt,keys[i])==0);
    VERIFY(ios27_20d_destroy(rt)==0);
}
static atomic_int release_join;
static void *slow(void *ignored) {
    (void)ignored;
    while(!atomic_load(&release_join)) tiny_pause();
    return (void *)(uintptr_t)42;
}
typedef struct { ios27_20d_handle target; int code; void *value; } join_arg;
static void *joiner(void *p) {
    join_arg *j=p;
    j->code=ios27_20d_thread_join(rt,j->target,&j->value);
    return NULL;
}
static void test_competing_joins(void) {
    ios27_20d_handle victim;
    pthread_t j1,j2;
    join_arg a,b;
    VERIFY(ios27_20d_create(&rt,1)==0);
    atomic_store(&release_join,0);
    VERIFY(ios27_20d_thread_create(rt,&victim,slow,NULL)==0);
    a.target=victim;
    b.target=victim;
    a.value=b.value=NULL;
    a.code=b.code=-1;
    VERIFY(pthread_create(&j1,NULL,joiner,&a)==0);
    VERIFY(pthread_create(&j2,NULL,joiner,&b)==0);
    atomic_store(&release_join,1);
    VERIFY(pthread_join(j1,NULL)==0);
    VERIFY(pthread_join(j2,NULL)==0);
    VERIFY((a.code==0)!=(b.code==0));
    VERIFY(a.code==0 || a.code==ESRCH || a.code==EINVAL);
    VERIFY(b.code==0 || b.code==ESRCH || b.code==EINVAL);
    VERIFY((uintptr_t)(a.code==0?a.value:b.value)==42u);
    VERIFY(ios27_20d_destroy(rt)==0);
}
int main(void) {
    test_wait_and_broadcast();
    test_quotas_generations();
    test_competing_joins();
    puts("phase20d: condition wake, limits, generation reuse and competing joins passed");
    return 0;
}
