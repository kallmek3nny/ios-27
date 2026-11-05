#include "ios27/phase27e_demo.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    p27_scene scene;
    p27_scene_init(&scene);
    const uint32_t w=320,h=240,stride=w*4;
    const size_t bytes=(size_t)stride*h;
    uint8_t *a=(uint8_t*)malloc(bytes), *b=(uint8_t*)malloc(bytes);
    assert(a && b);
    assert(p27_scene_render(&scene,a,bytes,w,h,stride,100));
    assert(p27_scene_render(&scene,b,bytes,w,h,stride,100));
    assert(!memcmp(a,b,bytes));
    uint64_t idle=p27_frame_hash(a,bytes);
    assert(idle != 0 && a[3] == 255);
    assert(p27_scene_pointer(&scene,1,0,.5f,.5f,101));
    assert(!p27_scene_pointer(&scene,1,0,.5f,.5f,101));
    for (int i=1;i<16;++i) assert(p27_scene_pointer(&scene,1,i,.2f,.1f,101));
    assert(!p27_scene_pointer(&scene,1,17,.1f,.1f,101));
    assert(!p27_scene_pointer(&scene,2,0,NAN,.5f,101));
    assert(!p27_scene_render(&scene,a,bytes-1,w,h,stride,101));
    assert(!p27_scene_render(&scene,a,bytes,8192,8192,32768,101));
    assert(p27_scene_render(&scene,a,bytes,w,h,stride,101));
    uint64_t touched=p27_frame_hash(a,bytes);
    assert(touched != idle);
    assert(p27_scene_pointer(&scene,3,0,.5f,.5f,110));
    assert(!p27_scene_pointer(&scene,2,0,.4f,.4f,111));
    for(int i=1;i<16;++i) assert(p27_scene_pointer(&scene,4,i,.2f,.1f,111));
    assert(p27_scene_render(&scene,b,bytes,w,h,stride,1000));
    assert(p27_frame_hash(b,bytes)==idle);
    printf("phase27e reference ok idle=%016llx touch=%016llx\n",
           (unsigned long long)idle,(unsigned long long)touched);
    free(a);free(b);
    return 0;
}
