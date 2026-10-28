#include "ios27/objc20b_dispatch.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static char root_class, root_meta, child_class, child_meta, leaf_class, leaf_meta;
static ios27_objc20b_runtime *runtime;
static ios27_objc20b_sel val, rec, sub, tag, cross;
static char logbuf[64]; static size_t logn;
static int fail_once;
static int root_init(void *ctx,void *cls) {
    assert(ctx==&root_class && cls==&root_class);
    logbuf[logn++]='r'; return 0;
}
static int child_init(void *ctx,void *cls) {
    uintptr_t out;
    assert(ctx==&child_class && cls==&child_class);
    logbuf[logn++]='c';
    /* Initialization is once, including same-thread nested send. */
    assert(ios27_objc20b_send_words(runtime,&root_class,tag,NULL,NULL,0,&out)==0);
    assert(out==20);
    if (fail_once) { fail_once=0; return -1; }
    return 0;
}
static uintptr_t root_value(void *self,ios27_objc20b_sel s,uintptr_t n) {
    assert(self && s==val); return n+2;
}
static uintptr_t child_value(void *self,ios27_objc20b_sel s,uintptr_t n) {
    uintptr_t out=0;
    assert(s==val);
    assert(ios27_objc20b_send_words(runtime,self,val,"Child",&n,1,&out)==0);
    return out+100;
}
static uintptr_t leaf_value(void *self,ios27_objc20b_sel s,uintptr_t n) {
    uintptr_t out=0;
    assert(s==val);
    assert(ios27_objc20b_send_words(runtime,self,val,"Leaf",&n,1,&out)==0);
    return out+1000;
}
static uintptr_t replacement(void *self,ios27_objc20b_sel s,uintptr_t n) {
    (void)self; assert(s==val); return n+7;
}
static uintptr_t recursive(void *self,ios27_objc20b_sel s,uintptr_t n) {
    uintptr_t out=0; assert(s==rec);
    if (!n) return 1;
    --n; assert(ios27_objc20b_send_words(runtime,self,rec,NULL,&n,1,&out)==0);
    return out+1;
}
static uintptr_t root_tag(void *self,ios27_objc20b_sel s) {
    assert(self != NULL && s==tag); return 20;
}
static uintptr_t child_tag(void *self,ios27_objc20b_sel s) {
    uintptr_t out=0;
    assert(self != NULL && s==tag);
    assert(ios27_objc20b_send_words(runtime,self,tag,"Child",NULL,0,&out)==0);
    return out+1;
}
static uintptr_t nested(void *self,ios27_objc20b_sel s,uintptr_t receiver,uintptr_t n) {
    uintptr_t out=0;
    (void)self; assert(s==cross);
    assert(ios27_objc20b_send_words(runtime,(void *)receiver,val,NULL,&n,1,&out)==0);
    return out+10;
}
static void good_register(void) {
    const ios27_objc20b_method root_i[]={
        {"value:",(ios27_objc20b_imp)root_value,1},
        {"recursive:",(ios27_objc20b_imp)recursive,1}};
    const ios27_objc20b_method root_c[]={{"classTag",(ios27_objc20b_imp)root_tag,0}};
    const ios27_objc20b_method child_i[]={
        {"value:",(ios27_objc20b_imp)child_value,1},
        {"cross:input:",(ios27_objc20b_imp)nested,2}};
    const ios27_objc20b_method child_c[]={{"classTag",(ios27_objc20b_imp)child_tag,0}};
    const ios27_objc20b_method leaf_i[]={{"value:",(ios27_objc20b_imp)leaf_value,1}};
    const ios27_objc20b_class specs[]={
        {"Leaf","Child",&leaf_class,&leaf_meta,32,leaf_i,1,NULL,0,NULL,NULL},
        {"Child","Root",&child_class,&child_meta,24,child_i,2,child_c,1,child_init,&child_class},
        {"Root",NULL,&root_class,&root_meta,16,root_i,2,root_c,1,root_init,&root_class}
    };
    assert(ios27_objc20b_register(runtime,specs,3)==0);
}
static void malformed_registration(void) {
    ios27_objc20b_stats before,after;
    ios27_objc20b_statistics(runtime,&before);
    const ios27_objc20b_class duplicate={"Root",NULL,&leaf_class,&leaf_meta,16,NULL,0,NULL,0,NULL,NULL};
    assert(ios27_objc20b_register(runtime,&duplicate,1)<0);
    const ios27_objc20b_class missing={"Missing","Who",(void *)&fail_once,(void *)&logbuf,16,NULL,0,NULL,0,NULL,NULL};
    assert(ios27_objc20b_register(runtime,&missing,1)<0);
    const ios27_objc20b_class cycle[]={
        {"CycleA","CycleB",(void *)&logbuf[0],(void *)&logbuf[1],16,NULL,0,NULL,0,NULL,NULL},
        {"CycleB","CycleA",(void *)&logbuf[2],(void *)&logbuf[3],16,NULL,0,NULL,0,NULL,NULL}};
    assert(ios27_objc20b_register(runtime,cycle,2)<0);
    const ios27_objc20b_method dupe[]={
        {"value:",(ios27_objc20b_imp)root_value,1},
        {"value:",(ios27_objc20b_imp)root_value,1}};
    const ios27_objc20b_class bad={"Bad",NULL,&logbuf[5],&logbuf[6],16,dupe,2,NULL,0,NULL,NULL};
    assert(ios27_objc20b_register(runtime,&bad,1)<0);
    ios27_objc20b_statistics(runtime,&after);
    assert(before.classes==after.classes && before.selectors==after.selectors);
}
int main(void) {
    assert(ios27_objc20b_create(&runtime)==0);
    assert(ios27_objc20b_set_global(runtime)==0);
    good_register(); malformed_registration();
    assert(ios27_objc20b_selector(runtime,"value:",&val)==0);
    assert(ios27_objc20b_selector(runtime,"recursive:",&rec)==0);
    assert(ios27_objc20b_selector(runtime,"classTag",&tag)==0);
    assert(ios27_objc20b_selector(runtime,"cross:input:",&cross)==0);
    assert(ios27_objc20b_selector(runtime,"sub",&sub)==0);
    assert(ios27_objc20b_selector(runtime,"",&sub)<0);
    char over[IOS27_OBJC20B_NAME+3]; memset(over,'x',sizeof(over));
    assert(ios27_objc20b_selector(runtime,over,&sub)<0);
    assert(ios27_objc20b_selector(runtime,"value:",&sub)==0 && sub==val);
    void *a=ios27_objc20b_alloc(runtime,"Root");
    void *b=ios27_objc20b_alloc(runtime,"Child");
    void *c=ios27_objc20b_alloc(runtime,"Leaf");
    assert(a && b && c && *(void **)a==&root_class && *(void **)c==&leaf_class);
    uintptr_t out=0,n=5;
    assert(ios27_objc20b_send_words(runtime,a,val,NULL,&n,1,&out)==0 && out==7);
    fail_once=1;
    assert(ios27_objc20b_send_words(runtime,b,val,NULL,&n,1,&out)<0);
    assert(ios27_objc20b_send_words(runtime,b,val,NULL,&n,1,&out)==0 && out==107);
    assert(logn==3 && !memcmp(logbuf,"rcc",3));
    assert(ios27_objc20b_send_words(runtime,c,val,NULL,&n,1,&out)==0 && out==1107);
    assert(ios27_objc20b_send_words(runtime,b,val,"Child",&n,1,&out)==0 && out==7);
    assert(ios27_objc20b_send_words(runtime,c,val,"Child",&n,1,&out)==0 && out==7);
    assert(ios27_objc20b_send_words(runtime,a,val,"Child",&n,1,&out)<0);
    assert(ios27_objc20b_send_words(runtime,b,val,"Root",&n,1,&out)<0);
    assert(ios27_objc20b_send_words(runtime,&root_class,tag,NULL,NULL,0,&out)==0 && out==20);
    assert(ios27_objc20b_send_words(runtime,&child_class,tag,NULL,NULL,0,&out)==0 && out==21);
    assert(ios27_objc20b_send_words(runtime,&leaf_class,tag,NULL,NULL,0,&out)==0 && out==21);
    assert(ios27_objc20b_send_words(runtime,&child_class,val,NULL,&n,1,&out)<0);
    n=30; assert(ios27_objc20b_send_words(runtime,c,rec,NULL,&n,1,&out)==0 && out==31);
    uintptr_t args[]={ (uintptr_t)a, 7 };
    assert(ios27_objc20b_send_words(runtime,b,cross,NULL,args,2,&out)==0 && out==19);
    assert(ios27_objc20b_send_words(runtime,b,cross,NULL,args,1,&out)<0);
    assert(ios27_objc20b_send_words(runtime,(void *)(uintptr_t)0x12345,val,NULL,&n,1,&out)<0);
    assert(ios27_objc20b_send_words(runtime,b,(void *)(uintptr_t)0x12345,NULL,&n,1,&out)<0);
    for (int i=0;i<12000;++i) {
        n=(uintptr_t)(i%9);
        assert(ios27_objc20b_send_words(runtime,c,val,NULL,&n,1,&out)==0 && out==n+1102);
    }
    ios27_objc20b_stats before,after; ios27_objc20b_statistics(runtime,&before);
    assert(before.cache_hits>10000 && before.cache_misses>0);
    const ios27_objc20b_method change={"value:",(ios27_objc20b_imp)replacement,1};
    assert(ios27_objc20b_replace(runtime,"Root",0,&change)==0);
    n=5; assert(ios27_objc20b_send_words(runtime,c,val,NULL,&n,1,&out)==0 && out==1112);
    ios27_objc20b_statistics(runtime,&after);
    assert(after.epoch>before.epoch);
    assert(ios27_objc20b_replace(runtime,"Root",0,NULL)<0);
    assert(ios27_objc20b_release(runtime,a)==0);
    assert(ios27_objc20b_release(runtime,a)<0);
    assert(ios27_objc20b_release(runtime,b)==0);
    assert(ios27_objc20b_release(runtime,c)==0);
    ios27_objc20b_statistics(runtime,&after); assert(after.objects==0);
    int foreign=7;
    assert(ios27_objc20b_bind(runtime,&foreign,"Root")==0);
    assert(ios27_objc20b_bind(runtime,&foreign,"Root")<0);
    assert(ios27_objc20b_release(runtime,&foreign)==0);
    for (size_t i=0;i<IOS27_OBJC20B_OBJECTS;++i)
        assert(ios27_objc20b_alloc(runtime,"Root")!=NULL);
    assert(ios27_objc20b_alloc(runtime,"Root")==NULL);
    ios27_objc20b_statistics(runtime,&after);
    assert(after.objects==IOS27_OBJC20B_OBJECTS);
    ios27_objc20b_destroy(runtime);
    puts("phase20b: native dispatch registry, cache, init, super, rollback, capacity ok");
    return 0;
}
