#include "ios27/phase27d_scene.h"
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

typedef struct {
    bool active;
    p27d_anim_kind kind;
    p27d_curve curve;
    double from, to;
    phase26d_affine start_transform, end_transform;
    uint64_t start, duration;
} animation;
typedef struct {
    bool live;
    uint32_t generation;
    p27d_id parent;
    p27d_view_desc desc;
    animation anim[P27D_MAX_ANIMATIONS];
} view;
typedef struct {
    view v[P27D_MAX_VIEWS];
    p27d_id window;
    p27d_window_state state;
    p27d_id controller;
    p27d_controller_state controller_state;
} tree;
typedef struct {
    bool active;
    uint32_t pointer_id;
    p27d_id target;
} capture;
struct p27d_scene {
    pthread_mutex_t mutex;
    pthread_t owner;
    tree model, working, presented, save[P27D_MAX_TX_DEPTH];
    uint32_t generations[P27D_MAX_VIEWS];
    uint32_t depth;
    phase26d_context *backend;
    uint64_t revision, now;
    capture pointers[P27D_MAX_POINTERS];
};
typedef struct { double a,b,c,d,tx,ty; } matrix;
static bool finite(double x) { return isfinite(x)!=0; }
static bool valid_affine(phase26d_affine a) {
    double v[]={a.a,a.b,a.c,a.d,a.tx,a.ty};
    for (size_t i=0;i<6;i++) if (!finite(v[i]) || fabs(v[i])>100000) return false;
    double det=a.a*a.d-a.b*a.c;
    return finite(det) && fabs(det)>=0.000001 && fabs(det)<=1000000;
}
static bool valid_desc(const p27d_view_desc *d) {
    if (!d) return false;
    const phase26d_layer_desc *l=&d->layer;
    return l->width>=0 && l->height>=0 && l->width<=4096 && l->height<=4096 &&
           l->x>=-100000 && l->x<=100000 && l->y>=-100000 && l->y<=100000 &&
           l->corner_radius<=128 && l->blur_radius<=3 && l->shadow_radius<=8 &&
           l->shadow_x>=-32 && l->shadow_x<=32 &&
           l->shadow_y>=-32 && l->shadow_y<=32 &&
           l->clips<=1 && l->material<=1 && valid_affine(l->transform);
}
static bool same_id(p27d_id a,p27d_id b) {
    return a.slot==b.slot && a.generation==b.generation;
}
static view *find(tree *t,p27d_id id) {
    if (!id.generation || id.slot>=P27D_MAX_VIEWS) return NULL;
    view *v=&t->v[id.slot];
    return v->live && v->generation==id.generation ? v : NULL;
}
static const view *find_const(const tree *t,p27d_id id) {
    return find((tree *)t,id);
}
static bool owns(const p27d_scene *s) {
    return s->depth>0 && pthread_equal(s->owner,pthread_self())!=0;
}
p27d_view_desc p27d_default_desc(void) {
    p27d_view_desc d={0};
    d.layer=phase26d_default_desc();
    d.accepts_input=true;
    return d;
}
p27d_scene *p27d_new(void) {
    p27d_scene *s=calloc(1,sizeof(*s));
    if (!s) return NULL;
    if (pthread_mutex_init(&s->mutex,NULL)!=0) { free(s); return NULL; }
    s->backend=phase26d_new();
    if (!s->backend) {
        pthread_mutex_destroy(&s->mutex);
        free(s);
        return NULL;
    }
    return s;
}
void p27d_free(p27d_scene *s) {
    if (!s) return;
    pthread_mutex_lock(&s->mutex);
    phase26d_free(s->backend);
    pthread_mutex_unlock(&s->mutex);
    pthread_mutex_destroy(&s->mutex);
    free(s);
}
p27d_result p27d_begin(p27d_scene *s) {
    if (!s) return P27D_INVALID;
    pthread_mutex_lock(&s->mutex);
    p27d_result r=P27D_OK;
    if (s->depth && !owns(s)) r=P27D_STATE;
    else if (s->depth==P27D_MAX_TX_DEPTH) r=P27D_LIMIT;
    else {
        if (!s->depth) {
            s->owner=pthread_self();
            s->working=s->model;
        }
        s->save[s->depth]=s->working;
        ++s->depth;
    }
    pthread_mutex_unlock(&s->mutex);
    return r;
}
p27d_result p27d_rollback(p27d_scene *s) {
    if (!s) return P27D_INVALID;
    pthread_mutex_lock(&s->mutex);
    p27d_result r=P27D_STATE;
    if (owns(s)) {
        s->working=s->save[s->depth-1];
        --s->depth;
        r=P27D_OK;
    }
    pthread_mutex_unlock(&s->mutex);
    return r;
}
p27d_result p27d_create(p27d_scene *s,const p27d_view_desc *d,p27d_id *id) {
    if (!s || !id || !valid_desc(d)) return P27D_INVALID;
    pthread_mutex_lock(&s->mutex);
    p27d_result r=P27D_STATE;
    if (owns(s)) {
        r=P27D_LIMIT;
        for (uint32_t i=0;i<P27D_MAX_VIEWS;i++) {
            if (s->working.v[i].live) continue;
            if (s->generations[i]==UINT32_MAX) continue;
            uint32_t gen=++s->generations[i];
            view *v=&s->working.v[i];
            memset(v,0,sizeof(*v));
            v->live=true; v->generation=gen; v->desc=*d;
            *id=(p27d_id){i,gen};
            r=P27D_OK;
            break;
        }
    }
    pthread_mutex_unlock(&s->mutex);
    return r;
}
p27d_result p27d_set(p27d_scene *s,p27d_id id,const p27d_view_desc *d) {
    if (!s || !valid_desc(d)) return P27D_INVALID;
    pthread_mutex_lock(&s->mutex);
    p27d_result r=P27D_STATE;
    if (owns(s)) {
        view *v=find(&s->working,id);
        r=v?P27D_OK:P27D_STALE;
        if (v) v->desc=*d;
    }
    pthread_mutex_unlock(&s->mutex);
    return r;
}
p27d_result p27d_parent(p27d_scene *s,p27d_id child,p27d_id parent) {
    if (!s || (!parent.generation && parent.slot)) return P27D_INVALID;
    pthread_mutex_lock(&s->mutex);
    p27d_result r=P27D_STATE;
    if (owns(s)) {
        view *v=find(&s->working,child);
        r=v?P27D_OK:P27D_STALE;
        if (v && parent.generation) {
            const view *p=find_const(&s->working,parent);
            if (!p) r=P27D_STALE;
            else {
                p27d_id cursor=parent;
                for (unsigned i=0;i<P27D_MAX_VIEWS;i++) {
                    if (same_id(cursor,child)) { r=P27D_INVALID; break; }
                    const view *node=find_const(&s->working,cursor);
                    if (!node || !node->parent.generation) break;
                    cursor=node->parent;
                }
            }
        }
        if (r==P27D_OK) v->parent=parent;
    }
    pthread_mutex_unlock(&s->mutex);
    return r;
}
p27d_result p27d_remove(p27d_scene *s,p27d_id id) {
    if (!s) return P27D_INVALID;
    pthread_mutex_lock(&s->mutex);
    p27d_result r=P27D_STATE;
    if (owns(s)) {
        view *v=find(&s->working,id);
        r=v?P27D_OK:P27D_STALE;
        if (v) {
            v->live=false;
            /* Parent-first repeated scan safely retires whole subtree. */
            for (unsigned n=0;n<P27D_MAX_VIEWS;n++) {
                bool changed=false;
                for (unsigned i=0;i<P27D_MAX_VIEWS;i++) {
                    view *q=&s->working.v[i];
                    if (!q->live || !q->parent.generation) continue;
                    const view *p=find_const(&s->working,q->parent);
                    if (!p) { q->live=false; changed=true; }
                }
                if (!changed) break;
            }
            if (s->working.window.generation &&
                !find_const(&s->working,s->working.window)) {
                s->working.window=(p27d_id){0,0};
                s->working.state=P27D_DETACHED;
            }
            if (s->working.controller.generation &&
                !find_const(&s->working,s->working.controller)) {
                s->working.controller=(p27d_id){0,0};
                s->working.controller_state=P27D_CONTROLLER_UNLOADED;
            }
        }
    }
    pthread_mutex_unlock(&s->mutex);
    return r;
}
p27d_result p27d_window(p27d_scene *s,p27d_id root,p27d_window_state state) {
    if (!s || state<P27D_DETACHED || state>P27D_VISIBLE ||
        (state==P27D_DETACHED && root.generation) ||
        (state!=P27D_DETACHED && !root.generation)) return P27D_INVALID;
    pthread_mutex_lock(&s->mutex);
    p27d_result r=P27D_STATE;
    if (owns(s)) {
        r=P27D_OK;
        if (state!=P27D_DETACHED) {
            view *v=find(&s->working,root);
            if (!v) r=P27D_STALE;
            else if (v->parent.generation) r=P27D_INVALID;
        }
        if (r==P27D_OK) {
            s->working.window=root;
            s->working.state=state;
            if (state!=P27D_VISIBLE &&
                s->working.controller_state==P27D_CONTROLLER_VISIBLE)
                s->working.controller_state=P27D_CONTROLLER_LOADED;
        }
    }
    pthread_mutex_unlock(&s->mutex);
    return r;
}
p27d_result p27d_window_get(p27d_scene *s,p27d_id *root,p27d_window_state *state) {
    if (!s || !root || !state) return P27D_INVALID;
    pthread_mutex_lock(&s->mutex);
    *root=s->model.window; *state=s->model.state;
    pthread_mutex_unlock(&s->mutex);
    return P27D_OK;
}
p27d_result p27d_controller_set(p27d_scene *s,p27d_id id,p27d_controller_state state) {
    if (!s || state<P27D_CONTROLLER_UNLOADED || state>P27D_CONTROLLER_VISIBLE ||
        (state==P27D_CONTROLLER_UNLOADED && id.generation) ||
        (state!=P27D_CONTROLLER_UNLOADED && !id.generation)) return P27D_INVALID;
    pthread_mutex_lock(&s->mutex);
    p27d_result r=P27D_STATE;
    if (owns(s)) {
        r=P27D_OK;
        if (state!=P27D_CONTROLLER_UNLOADED && !find_const(&s->working,id))
            r=P27D_STALE;
        else if (state==P27D_CONTROLLER_VISIBLE &&
                 (s->working.controller_state!=P27D_CONTROLLER_LOADED ||
                  !same_id(id,s->working.controller) ||
                  !same_id(id,s->working.window) ||
                  s->working.state!=P27D_VISIBLE)) r=P27D_INVALID;
        else if (state==P27D_CONTROLLER_LOADED &&
                 s->working.controller_state==P27D_CONTROLLER_VISIBLE &&
                 !same_id(id,s->working.controller)) r=P27D_INVALID;
        if (r==P27D_OK) {
            s->working.controller=id;
            s->working.controller_state=state;
        }
    }
    pthread_mutex_unlock(&s->mutex);
    return r;
}
p27d_result p27d_controller_get(p27d_scene *s,p27d_id *id,p27d_controller_state *state) {
    if (!s || !id || !state) return P27D_INVALID;
    pthread_mutex_lock(&s->mutex);
    *id=s->model.controller;
    *state=s->model.controller_state;
    pthread_mutex_unlock(&s->mutex);
    return P27D_OK;
}
static bool valid_anim(p27d_anim_kind kind,double from,double to) {
    if (!finite(from) || !finite(to)) return false;
    switch (kind) {
    case P27D_ANIM_X: case P27D_ANIM_Y:
        return fabs(from)<=100000 && fabs(to)<=100000;
    case P27D_ANIM_OPACITY:
        return from>=0 && from<=255 && to>=0 && to<=255;
    case P27D_ANIM_SCALE:
        return from>=0.01 && from<=16 && to>=0.01 && to<=16;
    default: return false;
    }
}
static p27d_result add_anim(p27d_scene *s,p27d_id id,animation a) {
    p27d_result r=P27D_STATE;
    if (owns(s)) {
        view *v=find(&s->working,id);
        r=v?P27D_LIMIT:P27D_STALE;
        if (v) {
            for (unsigned i=0;i<P27D_MAX_ANIMATIONS;i++) {
                if (!v->anim[i].active || v->anim[i].kind==a.kind) {
                    v->anim[i]=a; r=P27D_OK; break;
                }
            }
        }
    }
    return r;
}
p27d_result p27d_animate(p27d_scene *s,p27d_id id,p27d_anim_kind kind,
                          double from,double to,uint64_t start,uint64_t duration,
                          p27d_curve curve) {
    if (!s || !valid_anim(kind,from,to) || curve>P27D_SMOOTH ||
        !duration || start>UINT64_MAX-duration || curve<P27D_LINEAR) return P27D_INVALID;
    animation a={0};
    a.active=true; a.kind=kind; a.curve=curve;
    a.from=from; a.to=to; a.start=start; a.duration=duration;
    pthread_mutex_lock(&s->mutex);
    p27d_result r=add_anim(s,id,a);
    pthread_mutex_unlock(&s->mutex);
    return r;
}
p27d_result p27d_animate_transform(p27d_scene *s,p27d_id id,
                          phase26d_affine from,phase26d_affine to,
                          uint64_t start,uint64_t duration,p27d_curve curve) {
    if (!s || !valid_affine(from) || !valid_affine(to) ||
        curve>P27D_SMOOTH || curve<P27D_LINEAR ||
        !duration || start>UINT64_MAX-duration)
        return P27D_INVALID;
    animation a={0};
    a.active=true; a.kind=P27D_ANIM_TRANSFORM; a.curve=curve;
    a.start_transform=from; a.end_transform=to;
    a.start=start; a.duration=duration;
    pthread_mutex_lock(&s->mutex);
    p27d_result r=add_anim(s,id,a);
    pthread_mutex_unlock(&s->mutex);
    return r;
}
p27d_result p27d_cancel(p27d_scene *s,p27d_id id,p27d_anim_kind kind) {
    if (!s || kind<P27D_ANIM_X || kind>P27D_ANIM_TRANSFORM) return P27D_INVALID;
    pthread_mutex_lock(&s->mutex);
    p27d_result r=P27D_STATE;
    if (owns(s)) {
        view *v=find(&s->working,id);
        r=v?P27D_OK:P27D_STALE;
        if (v) for (unsigned i=0;i<P27D_MAX_ANIMATIONS;i++) {
            if (v->anim[i].active && v->anim[i].kind==kind) v->anim[i].active=false;
        }
    }
    pthread_mutex_unlock(&s->mutex);
    return r;
}
static double progress(const animation *a,uint64_t now) {
    double t=now<=a->start?0.0:
        now>=a->start+a->duration?1.0:(double)(now-a->start)/(double)a->duration;
    switch (a->curve) {
    case P27D_EASE_IN: return t*t;
    case P27D_EASE_OUT: return t*(2.0-t);
    case P27D_SMOOTH: return t*t*(3.0-2.0*t);
    default: return t;
    }
}
static phase26d_affine interpolate(phase26d_affine a,phase26d_affine b,double t) {
    return (phase26d_affine){
        a.a+(b.a-a.a)*t, a.b+(b.b-a.b)*t,
        a.c+(b.c-a.c)*t, a.d+(b.d-a.d)*t,
        a.tx+(b.tx-a.tx)*t, a.ty+(b.ty-a.ty)*t
    };
}
static void evaluated_view(view *v,uint64_t now) {
    for (unsigned j=0;j<P27D_MAX_ANIMATIONS;j++) {
        const animation *a=&v->anim[j];
        if (a->active && a->kind==P27D_ANIM_TRANSFORM)
            v->desc.layer.transform=interpolate(
                a->start_transform,a->end_transform,progress(a,now));
    }
    for (unsigned j=0;j<P27D_MAX_ANIMATIONS;j++) {
        const animation *a=&v->anim[j];
        if (!a->active || a->kind==P27D_ANIM_TRANSFORM) continue;
        double t=progress(a,now), value=a->from+(a->to-a->from)*t;
        switch (a->kind) {
        case P27D_ANIM_X: v->desc.layer.x=(int32_t)lround(value); break;
        case P27D_ANIM_Y: v->desc.layer.y=(int32_t)lround(value); break;
        case P27D_ANIM_OPACITY: v->desc.layer.opacity=(uint8_t)lround(value); break;
        case P27D_ANIM_SCALE:
            v->desc.layer.transform.a*=value;
            v->desc.layer.transform.b*=value;
            v->desc.layer.transform.c*=value;
            v->desc.layer.transform.d*=value;
            break;
        default: break;
        }
    }
}
static p27d_result build(const tree *logical,uint64_t now,tree *shown,
                          phase26d_context **out) {
    *shown=*logical;
    phase26d_context *backend=phase26d_new();
    if (!backend) return P27D_MEMORY;
    phase26d_id map[P27D_MAX_VIEWS]={{0,0}};
    phase26d_result r=phase26d_begin(backend);
    if (r!=PHASE26D_OK) { phase26d_free(backend); return P27D_STATE; }
    for (unsigned i=0;i<P27D_MAX_VIEWS;i++) {
        view *v=&shown->v[i];
        if (!v->live) continue;
        evaluated_view(v,now);
        if (!valid_desc(&v->desc)) { r=PHASE26D_INVALID; break; }
        phase26d_layer_desc d=v->desc.layer;
        /* Hidden parents multiply alpha into all descendants in phase26d. */
        if (v->desc.hidden || (shown->window.generation &&
            (!v->parent.generation && !same_id((p27d_id){i,v->generation},shown->window))) ||
            (shown->window.generation && shown->state!=P27D_VISIBLE))
            d.opacity=0;
        r=phase26d_create(backend,&d,&map[i]);
        if (r!=PHASE26D_OK) break;
    }
    if (r==PHASE26D_OK) for (unsigned i=0;i<P27D_MAX_VIEWS;i++) {
        const view *v=&shown->v[i];
        if (!v->live || !v->parent.generation) continue;
        if (v->parent.slot>=P27D_MAX_VIEWS ||
            !find_const(shown,v->parent)) { r=PHASE26D_INVALID; break; }
        r=phase26d_parent(backend,map[i],map[v->parent.slot]);
        if (r!=PHASE26D_OK) break;
    }
    if (r==PHASE26D_OK) r=phase26d_commit(backend);
    if (r!=PHASE26D_OK) {
        (void)phase26d_rollback(backend);
        phase26d_free(backend);
        return r==PHASE26D_MEMORY?P27D_MEMORY:r==PHASE26D_LIMIT?P27D_LIMIT:P27D_INVALID;
    }
    *out=backend;
    return P27D_OK;
}
p27d_result p27d_commit(p27d_scene *s) {
    if (!s) return P27D_INVALID;
    pthread_mutex_lock(&s->mutex);
    p27d_result r=P27D_STATE;
    if (owns(s)) {
        if (s->depth>1) { --s->depth; r=P27D_OK; }
        else {
            phase26d_context *backend=NULL;
            tree *shown=malloc(sizeof(*shown));
            if (!shown) r=P27D_MEMORY;
            else {
                r=build(&s->working,s->now,shown,&backend);
                if (r==P27D_OK) {
                    phase26d_context *old=s->backend;
                    s->backend=backend;
                    s->model=s->working;
                    s->presented=*shown;
                    ++s->revision;
                    --s->depth;
                    phase26d_free(old);
                }
                free(shown);
            }
        }
    }
    pthread_mutex_unlock(&s->mutex);
    return r;
}
p27d_result p27d_step(p27d_scene *s,uint64_t now) {
    if (!s) return P27D_INVALID;
    pthread_mutex_lock(&s->mutex);
    /* Rebuild into a private cache; failed evaluation cannot tear the old frame. */
    tree *shown=malloc(sizeof(*shown));
    phase26d_context *backend=NULL;
    p27d_result r=shown?build(&s->model,now,shown,&backend):P27D_MEMORY;
    if (r==P27D_OK) {
        phase26d_context *old=s->backend;
        s->backend=backend;
        s->presented=*shown;
        s->now=now;
        ++s->revision;
        phase26d_free(old);
    }
    free(shown);
    pthread_mutex_unlock(&s->mutex);
    return r;
}
p27d_result p27d_get(p27d_scene *s,p27d_id id,bool presentation,p27d_view_desc *out) {
    if (!s || !out) return P27D_INVALID;
    pthread_mutex_lock(&s->mutex);
    const view *v=find_const(presentation?&s->presented:&s->model,id);
    if (v) *out=v->desc;
    pthread_mutex_unlock(&s->mutex);
    return v?P27D_OK:P27D_STALE;
}
p27d_result p27d_next_frame(p27d_scene *s,uint64_t now,uint64_t *deadline,bool *needed) {
    if (!s || !deadline || !needed) return P27D_INVALID;
    pthread_mutex_lock(&s->mutex);
    *needed=false;
    *deadline=0;
    for (unsigned i=0;i<P27D_MAX_VIEWS;i++) {
        const view *v=&s->model.v[i];
        if (!v->live) continue;
        for (unsigned j=0;j<P27D_MAX_ANIMATIONS;j++) {
            const animation *a=&v->anim[j];
            if (!a->active || now>=a->start+a->duration) continue;
            uint64_t next=now<a->start?a->start:
                now<=UINT64_MAX-16666667u?now+16666667u:UINT64_MAX;
            if (next>a->start+a->duration) next=a->start+a->duration;
            if (!*needed || next<*deadline) *deadline=next;
            *needed=true;
        }
    }
    pthread_mutex_unlock(&s->mutex);
    return P27D_OK;
}
p27d_result p27d_damage_since(p27d_scene *s,uint64_t after,p27d_damage *out) {
    if (!s || !out) return P27D_INVALID;
    pthread_mutex_lock(&s->mutex);
    p27d_result r=P27D_OK;
    if (after>s->revision) r=P27D_INVALID;
    else *out=(p27d_damage){s->revision,s->now,after<s->revision};
    pthread_mutex_unlock(&s->mutex);
    return r;
}
static uint64_t hash_frame(phase26d_frame frame) {
    uint64_t h=UINT64_C(14695981039346656037);
    for (uint32_t y=0;y<frame.height;y++) {
        const uint8_t *row=frame.pixels+(size_t)y*frame.stride;
        for (uint32_t x=0;x<frame.width*4u;x++) {
            h^=row[x];
            h*=UINT64_C(1099511628211);
        }
    }
    return h;
}
p27d_result p27d_render(p27d_scene *s,phase26d_frame f,uint64_t *hash) {
    if (!s) return P27D_INVALID;
    pthread_mutex_lock(&s->mutex);
    phase26d_result r=phase26d_render(s->backend,f);
    if (r==PHASE26D_OK && hash) *hash=hash_frame(f);
    pthread_mutex_unlock(&s->mutex);
    return r==PHASE26D_OK?P27D_OK:P27D_INVALID;
}
p27d_result p27d_submit(p27d_scene *s,uint32_t width,uint32_t height,
                           p27d_surface_sink sink,void *userdata) {
    if (!s || !sink || !width || !height ||
        width>PHASE26D_MAX_FRAME_DIM || height>PHASE26D_MAX_FRAME_DIM)
        return P27D_INVALID;
    size_t bytes=(size_t)width*height*4u;
    uint8_t *pixels=malloc(bytes);
    if (!pixels) return P27D_MEMORY;
    phase26d_frame f={pixels,width,height,width*4u,bytes};
    p27d_surface_frame output={0};
    pthread_mutex_lock(&s->mutex);
    phase26d_result r=phase26d_render(s->backend,f);
    if (r==PHASE26D_OK) {
        output=(p27d_surface_frame){
            pixels,width,height,width*4u,bytes,s->now,s->revision,hash_frame(f)
        };
    }
    pthread_mutex_unlock(&s->mutex);
    /* Pixels are an immutable private snapshot; sink may reenter the scene. */
    bool accepted=r==PHASE26D_OK && sink(userdata,&output);
    free(pixels);
    return r!=PHASE26D_OK?P27D_INVALID:accepted?P27D_OK:P27D_STATE;
}
static matrix compose(matrix a,matrix b) {
    return (matrix){
        a.a*b.a+a.c*b.b,a.b*b.a+a.d*b.b,
        a.a*b.c+a.c*b.d,a.b*b.c+a.d*b.d,
        a.a*b.tx+a.c*b.ty+a.tx,a.b*b.tx+a.d*b.ty+a.ty
    };
}
static matrix local_matrix(const phase26d_layer_desc *d) {
    phase26d_affine a=d->transform;
    return (matrix){a.a,a.b,a.c,a.d,a.tx+d->x,a.ty+d->y};
}
static bool inverse(matrix m,double x,double y,double *ox,double *oy) {
    double det=m.a*m.d-m.b*m.c;
    if (!finite(det) || fabs(det)<0.000001) return false;
    x-=m.tx; y-=m.ty;
    *ox=(m.d*x-m.c*y)/det;
    *oy=(-m.b*x+m.a*y)/det;
    return finite(*ox) && finite(*oy);
}
static bool world_matrix(const tree *t,p27d_id id,matrix *out) {
    p27d_id parents[P27D_MAX_VIEWS];
    unsigned count=0;
    while (id.generation && count<P27D_MAX_VIEWS) {
        const view *v=find_const(t,id);
        if (!v) return false;
        parents[count++]=id;
        id=v->parent;
    }
    if (id.generation) return false;
    matrix m={1,0,0,1,0,0};
    while (count) {
        const view *v=find_const(t,parents[--count]);
        if (!v) return false;
        m=compose(m,local_matrix(&v->desc.layer));
    }
    *out=m;
    return true;
}
p27d_result p27d_convert(p27d_scene *s,p27d_id id,bool to_local,
                           double x,double y,double *out_x,double *out_y) {
    if (!s || !out_x || !out_y || !finite(x) || !finite(y)) return P27D_INVALID;
    pthread_mutex_lock(&s->mutex);
    matrix m;
    bool ok=world_matrix(&s->presented,id,&m);
    p27d_result r=ok?P27D_OK:P27D_STALE;
    if (ok) {
        if (to_local) {
            if (!inverse(m,x,y,out_x,out_y)) r=P27D_INVALID;
        } else {
            *out_x=m.a*x+m.c*y+m.tx; *out_y=m.b*x+m.d*y+m.ty;
            if (!finite(*out_x) || !finite(*out_y)) r=P27D_INVALID;
        }
    }
    pthread_mutex_unlock(&s->mutex);
    return r;
}
static bool in_bounds(double x,double y,const phase26d_layer_desc *d) {
    if (x<0 || y<0 || x>=d->width || y>=d->height) return false;
    double radius=fmin(d->corner_radius,fmin(d->width,d->height)/2.0);
    if (radius<=0) return true;
    double px=fmax(radius,fmin(x,d->width-radius));
    double py=fmax(radius,fmin(y,d->height-radius));
    double dx=x-px,dy=y-py;
    return dx*dx+dy*dy<=radius*radius;
}
static p27d_id hit_node(const tree *t,uint32_t idx,matrix parent,
                        double x,double y,unsigned depth) {
    p27d_id none={0,0};
    if (depth==P27D_MAX_VIEWS) return none;
    const view *v=&t->v[idx];
    if (!v->live || v->desc.hidden || v->desc.layer.opacity==0) return none;
    matrix m=compose(parent,local_matrix(&v->desc.layer));
    double lx,ly;
    if (!inverse(m,x,y,&lx,&ly)) return none;
    bool inside=in_bounds(lx,ly,&v->desc.layer);
    if (!inside && v->desc.layer.clips) return none;
    bool visited[P27D_MAX_VIEWS]={false};
    for (unsigned n=0;n<P27D_MAX_VIEWS;n++) {
        int best=-1;
        for (unsigned j=0;j<P27D_MAX_VIEWS;j++) {
            const view *ch=&t->v[j];
            if (visited[j] || !ch->live || !ch->parent.generation ||
                !same_id(ch->parent,(p27d_id){idx,v->generation})) continue;
            if (best<0 || ch->desc.layer.z>t->v[best].desc.layer.z ||
                (ch->desc.layer.z==t->v[best].desc.layer.z && j>(unsigned)best))
                best=(int)j;
        }
        if (best<0) break;
        visited[best]=true;
        p27d_id result=hit_node(t,(unsigned)best,m,x,y,depth+1);
        if (result.generation) return result;
    }
    return inside && v->desc.accepts_input?(p27d_id){idx,v->generation}:none;
}
static p27d_id hit_locked(const p27d_scene *s,double x,double y) {
    p27d_id none={0,0};
    const tree *t=&s->presented;
    if (t->window.generation && t->state!=P27D_VISIBLE) return none;
    matrix m={1,0,0,1,0,0};
    bool visited[P27D_MAX_VIEWS]={false};
    for (unsigned n=0;n<P27D_MAX_VIEWS;n++) {
        int best=-1;
        for (unsigned j=0;j<P27D_MAX_VIEWS;j++) {
            const view *v=&t->v[j];
            if (visited[j] || !v->live || v->parent.generation) continue;
            if (t->window.generation &&
                !same_id((p27d_id){j,v->generation},t->window)) continue;
            if (best<0 || v->desc.layer.z>t->v[best].desc.layer.z ||
                (v->desc.layer.z==t->v[best].desc.layer.z && j>(unsigned)best))
                best=(int)j;
        }
        if (best<0) break;
        visited[best]=true;
        p27d_id result=hit_node(t,(unsigned)best,m,x,y,0);
        if (result.generation) return result;
    }
    return none;
}
p27d_result p27d_hit_test(p27d_scene *s,double x,double y,p27d_id *hit) {
    if (!s || !hit || !finite(x) || !finite(y)) return P27D_INVALID;
    pthread_mutex_lock(&s->mutex);
    *hit=hit_locked(s,x,y);
    pthread_mutex_unlock(&s->mutex);
    return P27D_OK;
}
p27d_result p27d_route_event(p27d_scene *s,const p27d_pointer_event *e,
                               p27d_event_route *route) {
    if (!s || !e || !route || !finite(e->x) || !finite(e->y) ||
        e->action<P27D_POINTER_DOWN || e->action>P27D_POINTER_CANCEL)
        return P27D_INVALID;
    pthread_mutex_lock(&s->mutex);
    memset(route,0,sizeof(*route));
    int slot=-1,free_slot=-1;
    for (unsigned i=0;i<P27D_MAX_POINTERS;i++) {
        if (s->pointers[i].active && s->pointers[i].pointer_id==e->pointer_id) slot=(int)i;
        if (!s->pointers[i].active && free_slot<0) free_slot=(int)i;
    }
    p27d_id hit={0,0};
    p27d_result r=P27D_OK;
    if (e->action==P27D_POINTER_DOWN) {
        if (slot>=0) r=P27D_STATE;
        else {
            hit=hit_locked(s,e->x,e->y);
            if (hit.generation) {
                if (free_slot<0) r=P27D_LIMIT;
                else s->pointers[free_slot]=(capture){true,e->pointer_id,hit};
            }
        }
    } else if (slot>=0) {
        hit=s->pointers[slot].target;
        route->captured=true;
        if (!find_const(&s->presented,hit)) {
            hit=(p27d_id){0,0}; r=P27D_STALE;
        }
        if (e->action==P27D_POINTER_UP || e->action==P27D_POINTER_CANCEL ||
            r==P27D_STALE) s->pointers[slot].active=false;
    } else if (e->action==P27D_POINTER_MOVE) {
        hit=hit_locked(s,e->x,e->y); /* hover route, no capture */
    }
    if (r==P27D_OK && hit.generation) {
        matrix m;
        if (!world_matrix(&s->presented,hit,&m) ||
            !inverse(m,e->x,e->y,&route->local_x,&route->local_y)) r=P27D_STALE;
        else {
            p27d_id cursor=hit;
            while (cursor.generation && route->count<P27D_MAX_VIEWS) {
                const view *v=find_const(&s->presented,cursor);
                if (!v) { r=P27D_STALE; route->count=0; break; }
                route->path[route->count++]=cursor;
                cursor=v->parent;
            }
        }
    }
    pthread_mutex_unlock(&s->mutex);
    return r;
}
