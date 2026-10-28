#include "ios27/objc20b_dispatch.h"
#include <stdlib.h>
#include <string.h>
#define NONE UINT16_MAX
typedef struct { uint16_t sel; ios27_objc20b_imp imp; uint8_t argc; } method;
typedef struct {
    char name[IOS27_OBJC20B_NAME]; uint16_t parent;
    void *cls, *meta; size_t size; uint8_t state;
    ios27_objc20b_initializer init; void *ctx;
    uint16_t ni, nc; method im[IOS27_OBJC20B_METHODS], cm[IOS27_OBJC20B_METHODS];
} klass;
typedef struct { void *pointer; uint16_t type; size_t bytes; uint8_t owned; } object;
typedef struct { uint16_t type, sel, owner, slot; uint8_t meta, valid; uint64_t epoch; } cache;
struct ios27_objc20b_runtime {
    size_t nclass, nsel, nobject; uint64_t epoch, hits, misses;
    char selectors[IOS27_OBJC20B_SELECTORS][IOS27_OBJC20B_NAME];
    klass classes[IOS27_OBJC20B_CLASSES]; object objects[IOS27_OBJC20B_OBJECTS];
    cache lines[IOS27_OBJC20B_CACHE];
};
static ios27_objc20b_runtime *global_runtime;
static int valid(const char *s) {
    if (!s) return 0;
    for (size_t i=0; i<IOS27_OBJC20B_NAME; ++i) {
        unsigned char c=(unsigned char)s[i];
        if (!c) return i>0;
        if (c<33 || c>126) return 0;
    }
    return 0;
}
static int class_id(const ios27_objc20b_runtime *r, const char *name) {
    if (!r || !valid(name)) return -1;
    for (size_t i=0;i<r->nclass;++i)
        if (!strcmp(r->classes[i].name,name)) return (int)i;
    return -1;
}
static int selector_id(const ios27_objc20b_runtime *r, ios27_objc20b_sel sel) {
    if (!r || !sel) return -1;
    for (size_t i=0;i<r->nsel;++i)
        if (sel==(const void *)r->selectors[i]) return (int)i;
    return -1;
}
static int intern(ios27_objc20b_runtime *r, const char *name) {
    if (!r || !valid(name)) return -1;
    for (size_t i=0;i<r->nsel;++i)
        if (!strcmp(r->selectors[i],name)) return (int)i;
    if (r->nsel==IOS27_OBJC20B_SELECTORS) return -1;
    size_t i=r->nsel++;
    memcpy(r->selectors[i],name,strlen(name)+1u);
    return (int)i;
}
int ios27_objc20b_create(ios27_objc20b_runtime **out) {
    if (!out) return -1;
    *out=calloc(1,sizeof(**out));
    if (!*out) return -1;
    (*out)->epoch=1;
    return 0;
}
void ios27_objc20b_destroy(ios27_objc20b_runtime *r) {
    if (!r) return;
    if (global_runtime==r) global_runtime=NULL;
    for (size_t i=0;i<r->nobject;++i)
        if (r->objects[i].owned) free(r->objects[i].pointer);
    free(r);
}
int ios27_objc20b_selector(ios27_objc20b_runtime *r, const char *name,
                          ios27_objc20b_sel *out) {
    int id;
    if (!out || !r || (id=intern(r,name))<0) return -1;
    *out=r->selectors[id];
    return 0;
}
static int methods(ios27_objc20b_runtime *r, method *dst,
                   const ios27_objc20b_method *src, size_t count) {
    if (count>IOS27_OBJC20B_METHODS || (count && !src)) return -1;
    for (size_t i=0;i<count;++i) {
        int sel;
        if (!src[i].imp || src[i].word_arguments>4u ||
            (sel=intern(r,src[i].name))<0) return -1;
        for (size_t j=0;j<i;++j) if (dst[j].sel==(uint16_t)sel) return -1;
        dst[i]=(method){(uint16_t)sel,src[i].imp,src[i].word_arguments};
    }
    return 0;
}
int ios27_objc20b_register(ios27_objc20b_runtime *r,
                          const ios27_objc20b_class *spec, size_t n) {
    if (!r || !spec || !n || n>IOS27_OBJC20B_CLASSES-r->nclass) return -1;
    ios27_objc20b_runtime *stage=malloc(sizeof(*stage));
    if (!stage) return -1;
    memcpy(stage,r,sizeof(*stage));
    size_t first=stage->nclass;
    int ok=-1;
    for (size_t i=0;i<n;++i) {
        const ios27_objc20b_class *s=&spec[i];
        klass *c=&stage->classes[first+i];
        if (!valid(s->name) || class_id(stage,s->name)>=0 ||
            (s->superclass && !valid(s->superclass)) ||
            !s->class_object || !s->metaclass_object ||
            s->class_object==s->metaclass_object ||
            s->instance_size<sizeof(void *) || s->instance_size>65536u ||
            s->instance_count>IOS27_OBJC20B_METHODS ||
            s->class_count>IOS27_OBJC20B_METHODS) goto done;
        for (size_t j=0;j<first+i;++j)
            if (stage->classes[j].cls==s->class_object ||
                stage->classes[j].meta==s->class_object ||
                stage->classes[j].cls==s->metaclass_object ||
                stage->classes[j].meta==s->metaclass_object) goto done;
        memcpy(c->name,s->name,strlen(s->name)+1u);
        c->cls=s->class_object; c->meta=s->metaclass_object;
        c->size=s->instance_size; c->parent=NONE;
        c->init=s->initialize; c->ctx=s->context;
        c->ni=(uint16_t)s->instance_count; c->nc=(uint16_t)s->class_count;
        if (methods(stage,c->im,s->instances,s->instance_count) ||
            methods(stage,c->cm,s->classes,s->class_count)) goto done;
        ++stage->nclass;
    }
    for (size_t i=0;i<n;++i) {
        const char *parent=spec[i].superclass;
        klass *c=&stage->classes[first+i];
        int id;
        if (!parent) continue;
        id=class_id(stage,parent);
        if (id<0 || (size_t)id==first+i || stage->classes[id].size>c->size)
            goto done;
        c->parent=(uint16_t)id;
    }
    for (size_t i=first;i<stage->nclass;++i) {
        uint16_t at=(uint16_t)i; size_t steps=0;
        while (at!=NONE) {
            if (++steps>stage->nclass) goto done;
            at=stage->classes[at].parent;
        }
    }
    stage->epoch++;
    memcpy(r,stage,sizeof(*stage));
    ok=0;
done:
    free(stage);
    return ok;
}
static int receiver_id(const ios27_objc20b_runtime *r,const void *p,
                       uint16_t *type,int *meta) {
    if (!r || !p) return -1;
    for (size_t i=0;i<r->nclass;++i)
        if (p==r->classes[i].cls) { *type=(uint16_t)i; *meta=1; return 0; }
    for (size_t i=0;i<r->nobject;++i)
        if (p==r->objects[i].pointer) {
            *type=r->objects[i].type; *meta=0; return 0;
        }
    return -1;
}
static int initialize(ios27_objc20b_runtime *r,uint16_t type) {
    klass *c=&r->classes[type];
    if (c->state==2 || c->state==1) return 0; /* same-thread reentry */
    c->state=1;
    if ((c->parent!=NONE && initialize(r,c->parent)) ||
        (c->init && c->init(c->ctx,c->cls))) {
        c->state=0;
        return -1;
    }
    c->state=2;
    return 0;
}
void *ios27_objc20b_alloc(ios27_objc20b_runtime *r,const char *name) {
    int id=class_id(r,name);
    if (id<0 || r->nobject==IOS27_OBJC20B_OBJECTS) return NULL;
    void *obj=calloc(1,r->classes[id].size);
    if (!obj) return NULL;
    memcpy(obj,&r->classes[id].cls,sizeof(void *));
    r->objects[r->nobject++]=(object){obj,(uint16_t)id,r->classes[id].size,1};
    return obj;
}
int ios27_objc20b_bind(ios27_objc20b_runtime *r,void *p,const char *name) {
    int id=class_id(r,name); uint16_t old; int meta;
    if (id<0 || !p || r->nobject==IOS27_OBJC20B_OBJECTS ||
        receiver_id(r,p,&old,&meta)==0) return -1;
    r->objects[r->nobject++]=(object){p,(uint16_t)id,0,0};
    return 0;
}
int ios27_objc20b_release(ios27_objc20b_runtime *r,void *p) {
    if (!r || !p) return -1;
    for (size_t i=0;i<r->nobject;++i) if (r->objects[i].pointer==p) {
        if (r->objects[i].owned) free(p);
        r->objects[i]=r->objects[--r->nobject];
        memset(&r->objects[r->nobject],0,sizeof(object));
        return 0;
    }
    return -1;
}
static size_t cache_hash(uint16_t type,uint16_t sel,int meta) {
    return ((size_t)type*131u+(size_t)sel*17u+(size_t)meta*257u)%IOS27_OBJC20B_CACHE;
}
int ios27_objc20b_resolve(ios27_objc20b_runtime *r,const void *receiver,
                         ios27_objc20b_sel selector,const char *super_current,
                         ios27_objc20b_imp *out,uint8_t *argc) {
    uint16_t type; int meta,sel;
    if (out) *out=NULL;
    if (argc) *argc=0;
    if (!r || !out || !argc || receiver_id(r,receiver,&type,&meta) ||
        (sel=selector_id(r,selector))<0) return -1;
    if (initialize(r,type)) return -1;
    if (super_current) {
        int current=class_id(r,super_current);
        uint16_t walk=type;
        if (current<0) return -1;
        while (walk!=NONE && walk!=(uint16_t)current)
            walk=r->classes[walk].parent;
        if (walk==NONE) return -1;
        type=r->classes[current].parent;
        if (type==NONE) return -1;
    }
    size_t h=cache_hash(type,(uint16_t)sel,meta);
    cache *line=&r->lines[h];
    if (line->valid && line->epoch==r->epoch && line->type==type &&
        line->sel==(uint16_t)sel && line->meta==(uint8_t)meta) {
        method *m=meta?r->classes[line->owner].cm:r->classes[line->owner].im;
        *out=m[line->slot].imp; *argc=m[line->slot].argc;
        ++r->hits;
        return 0;
    }
    ++r->misses;
    uint16_t walk=type;
    for (size_t steps=0;walk!=NONE && steps<r->nclass;++steps) {
        klass *c=&r->classes[walk];
        method *arr=meta?c->cm:c->im;
        size_t n=meta?c->nc:c->ni;
        for (size_t i=0;i<n;++i) if (arr[i].sel==(uint16_t)sel) {
            *line=(cache){type,(uint16_t)sel,walk,(uint16_t)i,
                          (uint8_t)meta,1,r->epoch};
            *out=arr[i].imp; *argc=arr[i].argc;
            return 0;
        }
        walk=c->parent;
    }
    return -1;
}
int ios27_objc20b_send_words(ios27_objc20b_runtime *r,void *receiver,
                            ios27_objc20b_sel sel,const char *super_current,
                            const uintptr_t *args,size_t n,uintptr_t *result) {
    ios27_objc20b_imp imp; uint8_t argc;
    if (!result || n>4 || (n && !args) ||
        ios27_objc20b_resolve(r,receiver,sel,super_current,&imp,&argc) || n!=argc)
        return -1;
    /* Each dispatch arity uses a matching integer/pointer AArch64 ABI call. */
    switch (n) {
    case 0: *result=((uintptr_t (*)(void *,ios27_objc20b_sel))imp)(receiver,sel); break;
    case 1: *result=((uintptr_t (*)(void *,ios27_objc20b_sel,uintptr_t))imp)(receiver,sel,args[0]); break;
    case 2: *result=((uintptr_t (*)(void *,ios27_objc20b_sel,uintptr_t,uintptr_t))imp)(receiver,sel,args[0],args[1]); break;
    case 3: *result=((uintptr_t (*)(void *,ios27_objc20b_sel,uintptr_t,uintptr_t,uintptr_t))imp)(receiver,sel,args[0],args[1],args[2]); break;
    case 4: *result=((uintptr_t (*)(void *,ios27_objc20b_sel,uintptr_t,uintptr_t,uintptr_t,uintptr_t))imp)(receiver,sel,args[0],args[1],args[2],args[3]); break;
    default: return -1;
    }
    return 0;
}
int ios27_objc20b_replace(ios27_objc20b_runtime *r,const char *name,
                         int class_method,const ios27_objc20b_method *spec) {
    int id=class_id(r,name),sel;
    if (id<0 || !spec || !spec->imp || spec->word_arguments>4 ||
        !valid(spec->name) || (class_method!=0 && class_method!=1)) return -1;
    klass *c=&r->classes[id];
    method *arr=class_method?c->cm:c->im;
    uint16_t *count=class_method?&c->nc:&c->ni;
    int existing=-1;
    for (size_t i=0;i<r->nsel;++i)
        if (!strcmp(r->selectors[i],spec->name)) { existing=(int)i; break; }
    if (existing>=0) for (size_t i=0;i<*count;++i)
        if (arr[i].sel==(uint16_t)existing) {
            arr[i].imp=spec->imp; arr[i].argc=spec->word_arguments;
            ++r->epoch; return 0;
        }
    if (*count==IOS27_OBJC20B_METHODS || (sel=intern(r,spec->name))<0)
        return -1;
    arr[(*count)++]=(method){(uint16_t)sel,spec->imp,spec->word_arguments};
    ++r->epoch;
    return 0;
}
void ios27_objc20b_statistics(const ios27_objc20b_runtime *r,
                              ios27_objc20b_stats *out) {
    if (!out) return;
    memset(out,0,sizeof(*out));
    if (!r) return;
    *out=(ios27_objc20b_stats){r->hits,r->misses,r->epoch,r->nclass,r->nsel,r->nobject};
}
int ios27_objc20b_set_global(ios27_objc20b_runtime *r) {
    global_runtime=r; return 0;
}
void *ios27_objc20b_resolve_global(void *receiver,ios27_objc20b_sel sel) {
    ios27_objc20b_imp imp; uint8_t n;
    if (!global_runtime || ios27_objc20b_resolve(global_runtime,receiver,sel,NULL,&imp,&n))
        return NULL;
    /* C forbids casting a function pointer to void*. Store in a union solely
     * on ARM64 Darwin where data/code pointers share an address ABI. */
#if defined(__aarch64__) && defined(__APPLE__)
    union { ios27_objc20b_imp function; void *pointer; } conv;
    conv.function=imp; return conv.pointer;
#else
    (void)imp; return NULL;
#endif
}
void *ios27_objc20b_resolve_super_global(const void *super,ios27_objc20b_sel sel) {
    const void *const *pair=(const void *const *)super;
    if (!pair || !pair[0] || !pair[1] || !global_runtime) return NULL;
    uint16_t type; int meta;
    if (receiver_id(global_runtime,pair[0],&type,&meta)) return NULL;
    for (size_t i=0;i<global_runtime->nclass;++i)
        if (global_runtime->classes[i].cls==pair[1]) {
            ios27_objc20b_imp imp; uint8_t n;
            if (ios27_objc20b_resolve(global_runtime,pair[0],sel,
                  global_runtime->classes[i].name,&imp,&n)) return NULL;
#if defined(__aarch64__) && defined(__APPLE__)
            union { ios27_objc20b_imp function; void *pointer; } conv;
            conv.function=imp; return conv.pointer;
#else
            (void)imp; return NULL;
#endif
        }
    return NULL;
}
