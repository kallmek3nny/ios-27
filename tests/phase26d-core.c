#include "ios27/phase26d_compositor.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { phase26d_result r_=(x); if (r_!=PHASE26D_OK) { \
    fprintf(stderr,"%s:%d unexpected %d: %s\\n",__FILE__,__LINE__,r_,#x); abort(); } } while (0)
static phase26d_id create(phase26d_context *c,phase26d_layer_desc d) {
    phase26d_id id; CHECK(phase26d_create(c,&d,&id)); return id;
}
static void expect_pixel(const uint8_t *buf,unsigned stride,unsigned x,unsigned y,
                         uint8_t r,uint8_t g,uint8_t b,uint8_t a) {
    const uint8_t *p=buf+(size_t)y*stride+x*4u;
    if (p[0]!=r || p[1]!=g || p[2]!=b || p[3]!=a) {
        fprintf(stderr,"pixel %u,%u: got %u,%u,%u,%u expected %u,%u,%u,%u\\n",
            x,y,p[0],p[1],p[2],p[3],r,g,b,a);
        abort();
    }
}
static void transaction_test(void) {
    phase26d_context *c=phase26d_new(); assert(c);
    phase26d_layer_desc d=phase26d_default_desc(); d.width=4; d.height=4;
    assert(phase26d_commit(c)==PHASE26D_STATE);
    CHECK(phase26d_begin(c));
    phase26d_id root=create(c,d), cancelled;
    CHECK(phase26d_begin(c));
    cancelled=create(c,d);
    CHECK(phase26d_parent(c,cancelled,root));
    assert(phase26d_parent(c,root,cancelled)==PHASE26D_INVALID);
    CHECK(phase26d_rollback(c));
    CHECK(phase26d_commit(c));
    assert(phase26d_get(c,cancelled,0,&d)==PHASE26D_STALE);
    CHECK(phase26d_get(c,root,0,&d));
    CHECK(phase26d_begin(c));
    phase26d_id newer=create(c,d);
    assert(newer.slot==cancelled.slot && newer.generation!=cancelled.generation);
    CHECK(phase26d_parent(c,newer,root));
    CHECK(phase26d_destroy(c,root));
    CHECK(phase26d_commit(c));
    assert(phase26d_get(c,newer,0,&d)==PHASE26D_STALE);
    phase26d_stats s;
    CHECK(phase26d_stats_read(c,&s));
    assert(s.model_layers==0 && s.transaction_depth==0 && s.commits==2);
    phase26d_free(c);
}
static void golden_test(void) {
    phase26d_context *c=phase26d_new(); assert(c);
    CHECK(phase26d_begin(c));
    phase26d_layer_desc d=phase26d_default_desc();
    d.width=4; d.height=4; d.rgba[0]=255;
    phase26d_id red=create(c,d);
    d.width=2; d.height=2; d.x=1; d.y=1;
    d.rgba[0]=0; d.rgba[2]=255; d.rgba[3]=128;
    phase26d_id blue=create(c,d);
    CHECK(phase26d_parent(c,blue,red));
    CHECK(phase26d_commit(c));
    uint8_t buf[4*4*4]; memset(buf,0xa5,sizeof buf);
    CHECK(phase26d_render(c,(phase26d_frame){buf,4,4,16,sizeof buf}));
    for (unsigned y=0;y<4;y++) for (unsigned x=0;x<4;x++) {
        if (x>=1 && x<3 && y>=1 && y<3) expect_pixel(buf,16,x,y,127,0,128,255);
        else expect_pixel(buf,16,x,y,255,0,0,255);
    }
    uint8_t golden[sizeof buf]; memcpy(golden,buf,sizeof golden);
    CHECK(phase26d_render(c,(phase26d_frame){buf,4,4,16,sizeof buf}));
    assert(memcmp(buf,golden,sizeof buf)==0);

    /* upload is a copy, not borrowed storage; bad premultiplication is rejected. */
    uint8_t tex[16]={0,255,0,255,0,255,0,255,0,255,0,255,0,255,0,255};
    CHECK(phase26d_begin(c));
    d=phase26d_default_desc(); d.x=0; d.y=0; d.width=2; d.height=2;
    d.rgba[3]=0; d.z=10;
    phase26d_id overlay=create(c,d);
    CHECK(phase26d_upload(c,overlay,tex,2,2,8,sizeof tex));
    assert(phase26d_upload(c,overlay,tex,2,2,8,15)==PHASE26D_INVALID);
    assert(phase26d_render(c,(phase26d_frame){buf,4,4,16,15})==PHASE26D_INVALID);
    tex[0]=255;
    uint8_t bad[4]={255,0,0,1};
    assert(phase26d_upload(c,overlay,bad,1,1,4,sizeof bad)==PHASE26D_INVALID);
    CHECK(phase26d_commit(c));
    CHECK(phase26d_render(c,(phase26d_frame){buf,4,4,16,sizeof buf}));
    expect_pixel(buf,16,0,0,0,255,0,255);
    expect_pixel(buf,16,1,1,0,255,0,255);
    CHECK(phase26d_begin(c));
    CHECK(phase26d_destroy(c,overlay));
    CHECK(phase26d_commit(c));
    CHECK(phase26d_render(c,(phase26d_frame){buf,4,4,16,sizeof buf}));
    assert(memcmp(buf,golden,sizeof buf)==0);
    phase26d_free(c);
}
static void animation_test(void) {
    phase26d_context *c=phase26d_new(); assert(c);
    phase26d_layer_desc d=phase26d_default_desc();
    d.width=4; d.height=4;
    CHECK(phase26d_begin(c));
    phase26d_id a=create(c,d), b=create(c,d);
    CHECK(phase26d_animate(c,a,PHASE26D_ANIM_OPACITY,0,200,1000,1000));
    CHECK(phase26d_animate(c,a,PHASE26D_ANIM_X,0,10,1000,1000));
    CHECK(phase26d_animate(c,b,PHASE26D_ANIM_Y,0,20,1000,1000));
    CHECK(phase26d_commit(c));
    CHECK(phase26d_step(c,1500));
    phase26d_layer_desc model,view;
    CHECK(phase26d_get(c,a,0,&model));
    CHECK(phase26d_get(c,a,1,&view));
    assert(model.x==0 && model.opacity==255);
    assert(view.x==5 && view.opacity==100);
    CHECK(phase26d_get(c,b,1,&view)); assert(view.y==10);
    CHECK(phase26d_step(c,3000));
    CHECK(phase26d_get(c,a,1,&view)); assert(view.x==10 && view.opacity==200);
    CHECK(phase26d_begin(c));
    CHECK(phase26d_cancel(c,a,PHASE26D_ANIM_OPACITY));
    CHECK(phase26d_commit(c));
    CHECK(phase26d_step(c,1500));
    CHECK(phase26d_get(c,a,1,&view)); assert(view.opacity==255 && view.x==5);
    assert(phase26d_animate(c,a,PHASE26D_ANIM_X,0,1,UINT64_MAX,10)==PHASE26D_INVALID);
    phase26d_free(c);
}
static uint32_t prng(uint32_t *s) { *s^=*s<<13; *s^=*s>>17; *s^=*s<<5; return *s; }
static void nested_scene(void) {
    phase26d_context *c=phase26d_new(); assert(c);
    phase26d_id ids[120];
    phase26d_layer_desc d=phase26d_default_desc();
    d.width=32; d.height=32;
    CHECK(phase26d_begin(c));
    for (unsigned i=0;i<120;i++) {
        d.z=(int32_t)i; d.clips=1; d.corner_radius=(uint8_t)(i%4u);
        d.rgba[0]=(uint8_t)(i*13u); d.rgba[1]=(uint8_t)(i*7u);
        ids[i]=create(c,d);
        if (i) CHECK(phase26d_parent(c,ids[i],ids[i-1]));
    }
    CHECK(phase26d_animate(c,ids[50],PHASE26D_ANIM_OPACITY,80,255,100,500));
    CHECK(phase26d_animate(c,ids[60],PHASE26D_ANIM_X,0,2,100,500));
    CHECK(phase26d_animate(c,ids[70],PHASE26D_ANIM_Y,0,2,100,500));
    CHECK(phase26d_commit(c));
    uint8_t frame[32*32*4], again[sizeof frame];
    CHECK(phase26d_step(c,350));
    CHECK(phase26d_render(c,(phase26d_frame){frame,32,32,128,sizeof frame}));
    CHECK(phase26d_render(c,(phase26d_frame){again,32,32,128,sizeof again}));
    assert(memcmp(frame,again,sizeof frame)==0);
    phase26d_stats stats; CHECK(phase26d_stats_read(c,&stats));
    assert(stats.model_layers==120 && stats.presented_layers==120);
    phase26d_free(c);
}
typedef struct { phase26d_context *c; phase26d_id id; int accepted; } denied_args;
static void *foreign_transaction(void *opaque) {
    denied_args *a=opaque;
    phase26d_layer_desc d=phase26d_default_desc();
    if (phase26d_begin(a->c)!=PHASE26D_STATE) a->accepted=1;
    if (phase26d_set(a->c,a->id,&d)!=PHASE26D_STATE) a->accepted=1;
    if (phase26d_commit(a->c)!=PHASE26D_STATE) a->accepted=1;
    if (phase26d_rollback(a->c)!=PHASE26D_STATE) a->accepted=1;
    return NULL;
}
static void foreign_owner_test(void) {
    phase26d_context *c=phase26d_new(); assert(c);
    phase26d_layer_desc d=phase26d_default_desc();
    CHECK(phase26d_begin(c));
    phase26d_id id=create(c,d);
    CHECK(phase26d_commit(c));
    CHECK(phase26d_begin(c));
    denied_args a={c,id,0};
    pthread_t thread;
    assert(pthread_create(&thread,NULL,foreign_transaction,&a)==0);
    assert(pthread_join(thread,NULL)==0);
    assert(!a.accepted);
    CHECK(phase26d_commit(c));
    phase26d_free(c);
}
static void damage_test(void) {
    phase26d_context *c=phase26d_new(); assert(c);
    phase26d_damage dirty;
    CHECK(phase26d_damage_since(c,0,&dirty));
    assert(!dirty.full && dirty.revision==0);
    CHECK(phase26d_begin(c));
    phase26d_layer_desc d=phase26d_default_desc();
    d.x=10; d.y=20; d.width=5; d.height=7;
    phase26d_id root=create(c,d);
    CHECK(phase26d_rollback(c));
    CHECK(phase26d_damage_since(c,0,&dirty));
    assert(dirty.revision==0);
    CHECK(phase26d_begin(c));
    root=create(c,d);
    CHECK(phase26d_commit(c));
    CHECK(phase26d_damage_since(c,0,&dirty));
    assert(dirty.revision==1 && !dirty.full);
    assert(dirty.x<=10 && dirty.y<=20 &&
           (int64_t)dirty.x+dirty.width>=15 &&
           (int64_t)dirty.y+dirty.height>=27);
    CHECK(phase26d_begin(c));
    d.x=30; CHECK(phase26d_set(c,root,&d)); CHECK(phase26d_commit(c));
    CHECK(phase26d_begin(c));
    d.x=50; CHECK(phase26d_set(c,root,&d)); CHECK(phase26d_commit(c));
    CHECK(phase26d_damage_since(c,1,&dirty));
    assert(dirty.revision==3 && dirty.full);
    CHECK(phase26d_damage_since(c,3,&dirty));
    assert(!dirty.full && !dirty.width);
    CHECK(phase26d_step(c,1));
    CHECK(phase26d_damage_since(c,3,&dirty));
    assert(dirty.revision==4 && dirty.full);
    CHECK(phase26d_begin(c));
    phase26d_id child=create(c,d);
    CHECK(phase26d_parent(c,child,root));
    CHECK(phase26d_commit(c));
    CHECK(phase26d_damage_since(c,4,&dirty));
    assert(dirty.revision==5 && dirty.full);
    phase26d_free(c);
}
static void stress_test(void) {
    phase26d_context *c=phase26d_new(); assert(c);
    phase26d_layer_desc d=phase26d_default_desc();
    CHECK(phase26d_begin(c));
    phase26d_id root=create(c,d);
    CHECK(phase26d_commit(c));
    for (unsigned i=0;i<50000;i++) {
        CHECK(phase26d_begin(c));
        d.x=(int32_t)(i%100u);
        CHECK(phase26d_set(c,root,&d));
        d.y=(int32_t)(i%90u);
        CHECK(phase26d_set(c,root,&d));
        if ((i%1000u)==500u) {
            CHECK(phase26d_begin(c));
            d.x=999; CHECK(phase26d_set(c,root,&d));
            CHECK(phase26d_rollback(c));
        }
        CHECK(phase26d_commit(c));
    }
    phase26d_stats stats;
    CHECK(phase26d_stats_read(c,&stats));
    assert(stats.commits==50001 && stats.mutations>=100001);
    CHECK(phase26d_get(c,root,0,&d));
    assert(d.x==99 && d.y==49);
    phase26d_free(c);
}
typedef struct { phase26d_context *c; phase26d_id id; int failed; } race_args;
static void *reader(void *opaque) {
    race_args *a=opaque;
    for (unsigned i=0;i<4000;i++) {
        phase26d_layer_desc d;
        phase26d_result r=phase26d_get(a->c,a->id,1,&d);
        if (r!=PHASE26D_OK && r!=PHASE26D_STALE) a->failed=1;
        phase26d_stats s;
        if (phase26d_stats_read(a->c,&s)!=PHASE26D_OK) a->failed=1;
    }
    return NULL;
}
static void lifetime_fuzz_and_concurrency(void) {
    phase26d_context *c=phase26d_new(); assert(c);
    phase26d_layer_desc d=phase26d_default_desc();
    CHECK(phase26d_begin(c));
    phase26d_id root=create(c,d);
    CHECK(phase26d_commit(c));
    race_args arg={c,root,0};
    pthread_t thread;
    assert(pthread_create(&thread,NULL,reader,&arg)==0);
    uint32_t seed=0x26d5a11u;
    for (unsigned i=0;i<2000;i++) {
        CHECK(phase26d_begin(c));
        d.x=(int32_t)(prng(&seed)%100u);
        phase26d_id node=create(c,d);
        CHECK(phase26d_parent(c,node,root));
        if (i&1u) CHECK(phase26d_rollback(c));
        else {
            CHECK(phase26d_destroy(c,node));
            CHECK(phase26d_commit(c));
        }
    }
    CHECK(phase26d_begin(c)); CHECK(phase26d_destroy(c,root)); CHECK(phase26d_commit(c));
    assert(pthread_join(thread,NULL)==0);
    assert(!arg.failed);
    assert(phase26d_get(c,root,0,&d)==PHASE26D_STALE);
    phase26d_stats s; CHECK(phase26d_stats_read(c,&s));
    assert(!s.model_layers);
    phase26d_free(c);
}
int main(void) {
    transaction_test();
    golden_test();
    animation_test();
    nested_scene();
    foreign_owner_test();
    damage_test();
    stress_test();
    lifetime_fuzz_and_concurrency();
    puts("phase26d: transactions, exact pixels, animations, 120 layers, 50k commits, 100k mutations, lifetime races PASS");
    return 0;
}
