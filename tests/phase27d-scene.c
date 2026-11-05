#include "ios27/phase27d_scene.h"
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(call) do { p27d_result check_result=(call); if (check_result!=P27D_OK) { \
    fprintf(stderr,"%s:%d: %s returned %d\\n",__FILE__,__LINE__,#call,check_result); abort(); \
} } while (0)

static p27d_id make(p27d_scene *s,p27d_view_desc d) {
    p27d_id id; CHECK(p27d_create(s,&d,&id)); return id;
}
static bool eq(p27d_id a,p27d_id b) {
    return a.slot==b.slot && a.generation==b.generation;
}
static void pixel(const uint8_t *buf,unsigned stride,unsigned x,unsigned y,
                  uint8_t r,uint8_t g,uint8_t b,uint8_t a) {
    const uint8_t *p=buf+(size_t)y*stride+x*4u;
    if (p[0]!=r || p[1]!=g || p[2]!=b || p[3]!=a) {
        fprintf(stderr,"pixel %u,%u got %u,%u,%u,%u expected %u,%u,%u,%u\\n",
            x,y,p[0],p[1],p[2],p[3],r,g,b,a);
        abort();
    }
}
static void hash_golden(void) {
    p27d_scene *s=p27d_new(); assert(s);
    p27d_view_desc d=p27d_default_desc();
    d.layer.width=2; d.layer.height=2;
    d.layer.rgba[0]=255; d.layer.rgba[1]=0;
    d.layer.rgba[2]=0; d.layer.rgba[3]=255;
    CHECK(p27d_begin(s));
    p27d_id root=make(s,d);
    CHECK(p27d_window(s,root,P27D_VISIBLE));
    CHECK(p27d_commit(s));
    uint8_t pixels[16]={0};
    uint64_t hash=0;
    CHECK(p27d_render(s,(phase26d_frame){pixels,2,2,8,sizeof(pixels)},&hash));
    for (unsigned y=0;y<2;y++) for (unsigned x=0;x<2;x++)
        pixel(pixels,8,x,y,255,0,0,255);
    assert(hash==UINT64_C(0xbcc941082279871d));
    uint8_t before[sizeof(pixels)]; memcpy(before,pixels,sizeof(pixels));
    assert(p27d_render(s,(phase26d_frame){pixels,2,2,8,15},&hash)==P27D_INVALID);
    assert(memcmp(pixels,before,sizeof(pixels))==0);
    CHECK(p27d_begin(s));
    CHECK(p27d_remove(s,root));
    CHECK(p27d_commit(s));
    assert(p27d_get(s,root,false,&d)==P27D_STALE);
    p27d_free(s);
}
typedef struct {
    p27d_scene *scene;
    uint64_t expected_hash;
    unsigned count;
} sink_state;
static bool sink(void *opaque,const p27d_surface_frame *frame) {
    sink_state *arg=opaque;
    assert(frame && frame->pixels && frame->width==32 && frame->height==24);
    assert(frame->stride==128 && frame->bytes==32u*24u*4u);
    assert(frame->fnv1a64==arg->expected_hash);
    assert(frame->revision>0);
    ++arg->count;
    /* Callback can reenter scene without deadlocking. */
    p27d_damage damage;
    CHECK(p27d_damage_since(arg->scene,frame->revision,&damage));
    assert(!damage.full || damage.revision!=frame->revision);
    return true;
}
static void write_frame(const char *dir,unsigned index,const uint8_t *rgba,size_t bytes) {
    if (!dir) return;
    char name[512];
    int n=snprintf(name,sizeof(name),"%s/frame-%02u.rgba",dir,index);
    assert(n>0 && (size_t)n<sizeof(name));
    FILE *f=fopen(name,"wb");
    assert(f);
    assert(fwrite(rgba,1,bytes,f)==bytes);
    assert(fclose(f)==0);
}
static void demo_and_events(const char *output_dir) {
    p27d_scene *s=p27d_new(); assert(s);
    p27d_view_desc d=p27d_default_desc();
    d.layer.width=32; d.layer.height=24;
    d.layer.rgba[0]=20; d.layer.rgba[1]=30; d.layer.rgba[2]=40;
    CHECK(p27d_begin(s));
    p27d_id root=make(s,d);
    d.layer.x=4; d.layer.y=4; d.layer.width=12; d.layer.height=12;
    d.layer.rgba[0]=200; d.layer.rgba[1]=50; d.layer.rgba[2]=0;
    d.layer.rgba[3]=200;
    d.layer.clips=1; d.layer.corner_radius=3;
    d.layer.shadow_alpha=100; d.layer.shadow_radius=2;
    p27d_id panel=make(s,d);
    CHECK(p27d_parent(s,panel,root));
    d=p27d_default_desc();
    d.layer.width=4; d.layer.height=4;
    d.layer.x=2; d.layer.y=2;
    d.layer.z=5;
    d.layer.rgba[0]=0; d.layer.rgba[1]=255; d.layer.rgba[2]=0;
    p27d_id control=make(s,d);
    CHECK(p27d_parent(s,control,panel));
    d.layer.x=2; d.layer.y=16;
    d.layer.z=10;
    d.layer.rgba[0]=255; d.layer.rgba[1]=200; d.layer.rgba[2]=0;
    p27d_id moving=make(s,d);
    CHECK(p27d_parent(s,moving,root));
    CHECK(p27d_window(s,root,P27D_VISIBLE));
    CHECK(p27d_controller_set(s,root,P27D_CONTROLLER_LOADED));
    CHECK(p27d_controller_set(s,root,P27D_CONTROLLER_VISIBLE));
    CHECK(p27d_animate(s,moving,P27D_ANIM_X,2,22,0,UINT64_C(1000000000),P27D_SMOOTH));
    CHECK(p27d_animate(s,moving,P27D_ANIM_SCALE,1,1.5,0,UINT64_C(1000000000),P27D_LINEAR));
    CHECK(p27d_animate(s,moving,P27D_ANIM_OPACITY,255,128,0,UINT64_C(1000000000),P27D_LINEAR));
    phase26d_affine initial={1,0,0,1,0,0}, final={1,0,0,1,2,0};
    CHECK(p27d_animate_transform(s,panel,initial,final,0,UINT64_C(1000000000),P27D_EASE_IN));
    CHECK(p27d_begin(s));
    d.layer.x=77;
    CHECK(p27d_set(s,control,&d));
    CHECK(p27d_rollback(s)); /* nested savepoint must not publish */
    CHECK(p27d_commit(s));
    p27d_id controller;
    p27d_controller_state controller_state;
    CHECK(p27d_controller_get(s,&controller,&controller_state));
    assert(eq(controller,root) && controller_state==P27D_CONTROLLER_VISIBLE);
    p27d_view_desc model,present;
    CHECK(p27d_get(s,moving,false,&model));
    assert(model.layer.x==2);
    CHECK(p27d_get(s,control,false,&model));
    assert(model.layer.x==2);
    uint8_t frames[3][32*24*4];
    uint64_t hashes[3]={0,0,0};
    const uint64_t times[]={0,UINT64_C(500000000),UINT64_C(1000000000)};
    for (unsigned i=0;i<3;i++) {
        CHECK(p27d_step(s,times[i]));
        memset(frames[i],0xaa,sizeof(frames[i]));
        CHECK(p27d_render(s,(phase26d_frame){frames[i],32,24,128,sizeof(frames[i])},&hashes[i]));
        write_frame(output_dir,i,frames[i],sizeof(frames[i]));
        printf("phase27d frame %u time=%" PRIu64 " fnv1a64=%016" PRIx64 "\\n",i,times[i],hashes[i]);
    }
    assert(hashes[0]!=hashes[1] && hashes[1]!=hashes[2]);
    pixel(frames[0],128,2,16,255,200,0,255);
    pixel(frames[0],128,7,7,0,255,0,255);
    pixel(frames[0],128,0,0,20,30,40,255); /* outside layer + shadow */
    /* The panel shadow can cover the dot's old position after it moves. */
    assert(memcmp(frames[0]+16u*128u+2u*4u,frames[1]+16u*128u+2u*4u,4)!=0);
    pixel(frames[1],128,0,20,20,30,40,255);
    pixel(frames[2],128,0,20,20,30,40,255);
    CHECK(p27d_get(s,moving,false,&model));
    CHECK(p27d_get(s,moving,true,&present));
    assert(model.layer.x==2 && present.layer.x==22);
    assert(fabs(present.layer.transform.a-1.5)<0.000001);
    CHECK(p27d_step(s,times[1]));
    uint8_t repeated[sizeof(frames[1])];
    uint64_t h=0;
    CHECK(p27d_render(s,(phase26d_frame){repeated,32,24,128,sizeof(repeated)},&h));
    assert(h==hashes[1] && memcmp(repeated,frames[1],sizeof(repeated))==0);

    p27d_id hit={0,0};
    CHECK(p27d_hit_test(s,8,8,&hit));
    assert(eq(hit,control));
    double lx,ly,wx,wy;
    CHECK(p27d_convert(s,control,true,8,8,&lx,&ly));
    CHECK(p27d_convert(s,control,false,lx,ly,&wx,&wy));
    assert(fabs(wx-8)<1e-9 && fabs(wy-8)<1e-9);

    p27d_event_route route;
    CHECK(p27d_route_event(s,&(p27d_pointer_event){
        P27D_POINTER_DOWN,times[1],12,8,8},&route));
    assert(route.count==3 && eq(route.path[0],control));
    CHECK(p27d_route_event(s,&(p27d_pointer_event){
        P27D_POINTER_MOVE,times[1],12,30,20},&route));
    assert(route.captured && eq(route.path[0],control));
    CHECK(p27d_route_event(s,&(p27d_pointer_event){
        P27D_POINTER_UP,times[1],12,30,20},&route));
    assert(route.captured);
    sink_state state={s,hashes[1],0};
    CHECK(p27d_submit(s,32,24,sink,&state));
    assert(state.count==1);

    uint64_t deadline;
    bool needed;
    CHECK(p27d_next_frame(s,500000000,&deadline,&needed));
    assert(needed && deadline>500000000 && deadline<=1000000000);
    CHECK(p27d_next_frame(s,1000000000,&deadline,&needed));
    assert(!needed);
    p27d_damage damage;
    CHECK(p27d_damage_since(s,0,&damage));
    assert(damage.full && damage.revision>0);
    CHECK(p27d_damage_since(s,damage.revision,&damage));
    assert(!damage.full);
    assert(p27d_damage_since(s,UINT64_MAX,&damage)==P27D_INVALID);

    CHECK(p27d_begin(s));
    CHECK(p27d_cancel(s,moving,P27D_ANIM_SCALE));
    CHECK(p27d_window(s,root,P27D_ATTACHED));
    CHECK(p27d_commit(s));
    CHECK(p27d_controller_get(s,&controller,&controller_state));
    assert(controller_state==P27D_CONTROLLER_LOADED);
    CHECK(p27d_render(s,(phase26d_frame){repeated,32,24,128,sizeof(repeated)},&h));
    pixel(repeated,128,0,0,0,0,0,0);
    CHECK(p27d_hit_test(s,8,8,&hit));
    assert(!hit.generation);
    CHECK(p27d_begin(s));
    CHECK(p27d_window(s,root,P27D_VISIBLE));
    CHECK(p27d_commit(s));
    CHECK(p27d_step(s,times[1]));
    CHECK(p27d_get(s,moving,true,&present));
    assert(fabs(present.layer.transform.a-1)<1e-9);
    CHECK(p27d_begin(s));
    CHECK(p27d_remove(s,panel));
    CHECK(p27d_commit(s));
    assert(p27d_get(s,control,false,&model)==P27D_STALE);
    CHECK(p27d_begin(s));
    CHECK(p27d_remove(s,root));
    CHECK(p27d_commit(s));
    CHECK(p27d_controller_get(s,&controller,&controller_state));
    assert(controller_state==P27D_CONTROLLER_UNLOADED && !controller.generation);
    p27d_free(s);
}
typedef struct { p27d_scene *scene; p27d_id id; int fail; } thread_arg;
static void *foreign_writer(void *opaque) {
    thread_arg *a=opaque;
    if (p27d_begin(a->scene)!=P27D_STATE) a->fail=1;
    if (p27d_commit(a->scene)!=P27D_STATE) a->fail=1;
    return NULL;
}
static void *reader_thread(void *opaque) {
    thread_arg *a=opaque;
    uint8_t pixels[4*4*4];
    for (unsigned i=0;i<1000;i++) {
        p27d_view_desc v;
        if (p27d_get(a->scene,a->id,true,&v)!=P27D_OK) a->fail=1;
        if (p27d_render(a->scene,(phase26d_frame){pixels,4,4,16,sizeof(pixels)},NULL)!=P27D_OK)
            a->fail=1;
    }
    return NULL;
}
static void concurrency_and_rejection(void) {
    p27d_scene *s=p27d_new(); assert(s);
    p27d_view_desc d=p27d_default_desc();
    d.layer.width=4; d.layer.height=4;
    CHECK(p27d_begin(s));
    p27d_id id=make(s,d);
    CHECK(p27d_commit(s));
    CHECK(p27d_begin(s));
    thread_arg arg={s,id,0};
    pthread_t t;
    assert(pthread_create(&t,NULL,foreign_writer,&arg)==0);
    assert(pthread_join(t,NULL)==0 && !arg.fail);
    CHECK(p27d_rollback(s));
    assert(p27d_animate(s,id,P27D_ANIM_X,0,10,0,100,P27D_LINEAR)==P27D_STATE);
    CHECK(p27d_begin(s));
    phase26d_affine a={1,0,0,1,0,0},b={-1,0,0,1,0,0};
    CHECK(p27d_animate_transform(s,id,a,b,0,100,P27D_LINEAR));
    CHECK(p27d_commit(s));
    CHECK(p27d_step(s,0));
    assert(p27d_step(s,50)==P27D_INVALID); /* singular interpolated matrix */
    CHECK(p27d_get(s,id,true,&d));
    assert(d.layer.transform.a==1);
    CHECK(p27d_begin(s)); CHECK(p27d_cancel(s,id,P27D_ANIM_TRANSFORM));
    CHECK(p27d_commit(s));
    thread_arg args={s,id,0};
    assert(pthread_create(&t,NULL,reader_thread,&args)==0);
    for (unsigned i=0;i<300;i++) CHECK(p27d_step(s,i));
    assert(pthread_join(t,NULL)==0 && !args.fail);
    p27d_free(s);
}
int main(int argc,char **argv) {
    hash_golden();
    demo_and_events(argc>1?argv[1]:NULL);
    concurrency_and_rejection();
    puts("phase27d: golden rgba, timed snapshots, hit routes, nested tx, lifecycle, invalid matrices and concurrent frames PASS");
    return 0;
}
