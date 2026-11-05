#include <jni.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ios27/phase26e_host.h"
#include "ios27/phase27e_demo.h"

/* Opaque monotonically increasing handles, never address-based JNI handles. */
#define MAX_HOSTS 4
struct runtime {
    uint64_t token;
    phase26e_host *host;
    phase26e_id scene, window, root;
    p27_scene demo;
    uint32_t width, height;
};
static pthread_mutex_t guard = PTHREAD_MUTEX_INITIALIZER;
static struct runtime slots[MAX_HOSTS];
static uint64_t next_token = 1;
static struct runtime *lookup(jlong token) {
    if (token <= 0) return NULL;
    for (int i = 0; i < MAX_HOSTS; ++i)
        if (slots[i].token == (uint64_t)token && slots[i].host) return &slots[i];
    return NULL;
}
static void cancel_demo(struct runtime *r) {
    for (unsigned i=0;i<P27_MAX_POINTERS;++i) r->demo.pointers[i].active=0;
}
JNIEXPORT jlong JNICALL Java_dev_ios27_phase27e_MainActivity_nativeCreate(JNIEnv *e,jclass c) {
    (void)e; (void)c;
    pthread_mutex_lock(&guard);
    struct runtime *r=NULL;
    for (int i=0;i<MAX_HOSTS;++i) if(!slots[i].host){r=&slots[i];break;}
    if (!r || next_token == UINT64_MAX) { pthread_mutex_unlock(&guard); return 0; }
    memset(r,0,sizeof(*r));
    r->host=phase26e_create();
    if (!r->host || phase26e_scene_add(r->host,&r->scene)!=P26_OK ||
        phase26e_window_add(r->host,r->scene,(phase26e_rect){0,0,8192,8192},&r->window)!=P26_OK ||
        phase26e_view_add(r->host,r->window,0,(phase26e_rect){0,0,8192,8192},0,&r->root)!=P26_OK ||
        phase26e_launch(r->host)!=P26_OK) {
        phase26e_destroy(r->host); memset(r,0,sizeof(*r));
        pthread_mutex_unlock(&guard); return 0;
    }
    p27_scene_init(&r->demo);
    r->token=next_token++;
    jlong token=(jlong)r->token;
    pthread_mutex_unlock(&guard);
    return token;
}
JNIEXPORT void JNICALL Java_dev_ios27_phase27e_MainActivity_nativeDestroy(JNIEnv *e,jclass c,jlong token) {
    (void)e; (void)c;
    pthread_mutex_lock(&guard);
    struct runtime *r=lookup(token);
    if (r) { phase26e_destroy(r->host); memset(r,0,sizeof(*r)); }
    pthread_mutex_unlock(&guard);
}
/* 0 pause, 1 resume, 2 terminate. Idempotent from the Java activity. */
JNIEXPORT jint JNICALL Java_dev_ios27_phase27e_MainActivity_nativeLifecycle(JNIEnv *e,jclass c,jlong token,jint action) {
    (void)e; (void)c;
    pthread_mutex_lock(&guard);
    struct runtime *r=lookup(token);
    int result=P26_INVALID;
    if (r) {
        phase26e_lifecycle state=phase26e_get_metrics(r->host).lifecycle;
        if (action==0) {
            cancel_demo(r);
            result=state==P26_BACKGROUND ? P26_OK : phase26e_pause(r->host);
        } else if (action==1)
            result=state==P26_FOREGROUND ? P26_OK : phase26e_resume(r->host);
        else if (action==2) { cancel_demo(r); result=phase26e_terminate(r->host); }
    }
    pthread_mutex_unlock(&guard);return result;
}
JNIEXPORT jlong JNICALL Java_dev_ios27_phase27e_MainActivity_nativeSurface(JNIEnv *e,jclass c,
    jlong token,jboolean available,jint w,jint h,jint rotation) {
    (void)e; (void)c;
    if (w<0 || h<0 || w>8192 || h>8192) return 0;
    pthread_mutex_lock(&guard);
    struct runtime *r=lookup(token);
    jlong result=0;
    if (r && phase26e_surface(r->host,available?1:0,(uint32_t)w,(uint32_t)h,(uint32_t)rotation)==P26_OK) {
        r->width=available ? (uint32_t)w : 0;
        r->height=available ? (uint32_t)h : 0;
        cancel_demo(r);
        result=available ? (jlong)phase26e_get_metrics(r->host).surface_generation : 0;
    }
    pthread_mutex_unlock(&guard);return result;
}
static void drain_events(struct runtime *r,uint64_t now) {
    phase26e_event events[16];
    size_t n;
    while ((n=phase26e_drain(r->host,events,16)) != 0) {
        for(size_t i=0;i<n;++i) {
            phase26e_event *ev=&events[i];
            if (ev->kind>=P26_DOWN && ev->kind<=P26_CANCEL && r->width && r->height) {
                float x=ev->x/(float)r->width, y=ev->y/(float)r->height;
                if (x < 0.f) x = 0.f; else if (x > 1.f) x = 1.f;
                if (y < 0.f) y = 0.f; else if (y > 1.f) y = 1.f;
                if (ev->kind==P26_CANCEL) { /* cancellation may carry zero coords */
                    for(unsigned j=0;j<P27_MAX_POINTERS;++j) if(r->demo.pointers[j].active && r->demo.pointers[j].id==ev->pointer){
                        x=r->demo.pointers[j].x;y=r->demo.pointers[j].y;break;
                    }
                }
                (void)p27_scene_pointer(&r->demo,(int)ev->kind,ev->pointer,x,y,now);
            }
        }
    }
}
JNIEXPORT jint JNICALL Java_dev_ios27_phase27e_MainActivity_nativePointer(JNIEnv *e,jclass c,
    jlong token,jint kind,jint id,jfloat x,jfloat y,jlong now) {
    (void)e; (void)c;
    pthread_mutex_lock(&guard);
    struct runtime *r=lookup(token);
    int result=P26_INVALID;
    if (r && now>=0 && isfinite(x) && isfinite(y) &&
        (kind!=P26_DOWN || (x>=0 && y>=0 && x<(float)r->width && y<(float)r->height))) {
        result=phase26e_pointer(r->host,(phase26e_event_kind)kind,id,x,y);
        if (result==P26_OK) drain_events(r,(uint64_t)now);
    }
    pthread_mutex_unlock(&guard);return result;
}
JNIEXPORT jint JNICALL Java_dev_ios27_phase27e_MainActivity_nativeKey(JNIEnv *e,jclass c,
    jlong token,jboolean down,jint key) {
    (void)e; (void)c;
    pthread_mutex_lock(&guard);
    struct runtime *r=lookup(token);
    int result=r?phase26e_keyboard(r->host,down?1:0,key):P26_INVALID;
    if(r) drain_events(r,0);
    pthread_mutex_unlock(&guard);return result;
}
static jint text_event(JNIEnv *e,jlong token,jstring text,jboolean composing) {
    if (!text) return P26_INVALID;
    jsize len=(*e)->GetStringLength(e,text);
    if (len<0 || len>(jsize)PHASE26E_MAX_TEXT) return P26_INVALID;
    const jchar *u=(*e)->GetStringChars(e,text,NULL);
    if(!u) return P26_INVALID;
    char bytes[PHASE26E_MAX_TEXT+1u];size_t n=0;
    int valid=1;
    for(jsize i=0;i<len && valid;++i) {
        uint32_t cp=u[i];
        if(cp>=0xD800u && cp<=0xDBFFu) {
            if(++i>=len || u[i]<0xDC00u || u[i]>0xDFFFu){valid=0;break;}
            cp=0x10000u+((cp-0xD800u)<<10u)+(u[i]-0xDC00u);
        } else if(cp>=0xDC00u && cp<=0xDFFFu) {valid=0;break;}
        if(!cp){valid=0;break;}
        size_t k=cp<0x80u?1u:cp<0x800u?2u:cp<0x10000u?3u:4u;
        if(k>PHASE26E_MAX_TEXT-n){valid=0;break;}
        if(k==1u)bytes[n++]=(char)cp;
        else {
            if(k==2u)bytes[n++]=(char)(0xC0u|(cp>>6u));
            else if(k==3u){bytes[n++]=(char)(0xE0u|(cp>>12u));bytes[n++]=(char)(0x80u|((cp>>6u)&63u));}
            else {bytes[n++]=(char)(0xF0u|(cp>>18u));bytes[n++]=(char)(0x80u|((cp>>12u)&63u));bytes[n++]=(char)(0x80u|((cp>>6u)&63u));}
            bytes[n++]=(char)(0x80u|(cp&63u));
        }
    }
    (*e)->ReleaseStringChars(e,text,u);
    if(!valid)return P26_INVALID;
    bytes[n]='\0';
    pthread_mutex_lock(&guard);
    struct runtime *r=lookup(token);
    int result=r?(composing?phase26e_compose(r->host,bytes,n):phase26e_text(r->host,bytes,n)):P26_INVALID;
    if(r)drain_events(r,0);
    pthread_mutex_unlock(&guard);return result;
}
JNIEXPORT jint JNICALL Java_dev_ios27_phase27e_MainActivity_nativeText(JNIEnv *e,jclass c,
    jlong token,jstring value,jboolean composing) {
    (void)c;return text_event(e,token,value,composing);
}
JNIEXPORT void JNICALL Java_dev_ios27_phase27e_MainActivity_nativeFocus(JNIEnv *e,jclass c,
    jlong token,jboolean focused) {
    (void)e; (void)c;
    pthread_mutex_lock(&guard);
    struct runtime *r=lookup(token);
    if(r){(void)phase26e_focus(r->host,focused?1:0);if(!focused)cancel_demo(r);drain_events(r,0);}
    pthread_mutex_unlock(&guard);
}
JNIEXPORT jint JNICALL Java_dev_ios27_phase27e_MainActivity_nativeAccessibility(JNIEnv *e,jclass c,jlong token) {
    (void)e; (void)c;
    pthread_mutex_lock(&guard);
    struct runtime *r=lookup(token);
    int result=r?phase26e_accessibility_focus(r->host,r->root):P26_INVALID;
    if(r)drain_events(r,0);
    pthread_mutex_unlock(&guard);return result;
}
JNIEXPORT jboolean JNICALL Java_dev_ios27_phase27e_MainActivity_nativeRender(JNIEnv *e,jclass c,
    jlong token,jlong generation,jobject buffer,jint w,jint h,jint stride,jlong now) {
    (void)c;
    if (!buffer || generation<=0 || now<0 || w<=0 || h<=0 || stride<=0) return JNI_FALSE;
    void *pixels=(*e)->GetDirectBufferAddress(e,buffer);
    jlong cap=(*e)->GetDirectBufferCapacity(e,buffer);
    if (!pixels || cap<0) return JNI_FALSE;
    pthread_mutex_lock(&guard);
    struct runtime *r=lookup(token);
    int success=0;
    if(r) {
        phase26e_metrics m=phase26e_get_metrics(r->host);
        if(m.lifecycle==P26_FOREGROUND && m.surface_active &&
           generation==(jlong)m.surface_generation &&
           (uint64_t)w*(uint64_t)h<=P27_MAX_PIXELS &&
           (uint64_t)stride*(uint64_t)h<=(uint64_t)cap) {
            (void)phase26e_request_frame(r->host);
            success=p27_scene_render(&r->demo,(uint8_t*)pixels,(size_t)cap,
                                     (uint32_t)w,(uint32_t)h,(uint32_t)stride,(uint64_t)now);
        }
    }
    pthread_mutex_unlock(&guard);return success?JNI_TRUE:JNI_FALSE;
}
JNIEXPORT jint JNICALL Java_dev_ios27_phase27e_MainActivity_nativePresented(JNIEnv *e,jclass c,
    jlong token,jlong generation) {
    (void)e; (void)c;
    if(generation<=0)return P26_INVALID;
    pthread_mutex_lock(&guard);
    struct runtime *r=lookup(token);
    int result=r?phase26e_present(r->host,(uint64_t)generation):P26_INVALID;
    pthread_mutex_unlock(&guard);return result;
}
JNIEXPORT jlongArray JNICALL Java_dev_ios27_phase27e_MainActivity_nativeMetrics(JNIEnv *e,jclass c,jlong token) {
    (void)c;
    pthread_mutex_lock(&guard);
    struct runtime *r=lookup(token);
    jlong vals[6]={0};
    if(r) {
        phase26e_metrics m=phase26e_get_metrics(r->host);
        vals[0]=(jlong)m.accepted_events;vals[1]=(jlong)m.frames_presented;
        vals[2]=(jlong)m.surface_recreations;vals[3]=(jlong)m.cancellations;
        vals[4]=(jlong)r->demo.tap_count;vals[5]=(jlong)m.dropped_log_events;
    }
    pthread_mutex_unlock(&guard);
    jlongArray out=(*e)->NewLongArray(e,6);
    if(out)(*e)->SetLongArrayRegion(e,out,0,6,vals);
    return out;
}
