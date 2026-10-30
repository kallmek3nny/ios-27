#include "ios27/phase26d_compositor.h"
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct image {
    uint32_t refs, width, height;
    size_t bytes;
    uint8_t pixels[];
} image;
typedef struct {
    bool active;
    phase26d_anim_kind kind;
    double from, to;
    uint64_t start, duration;
} animation;
typedef struct {
    bool used;
    uint32_t generation;
    phase26d_id parent;
    phase26d_layer_desc d;
    image *backing;
    animation anim[3];
} layer;
typedef struct { layer l[PHASE26D_MAX_LAYERS]; } tree;
struct phase26d_context {
    pthread_mutex_t mutex;
    pthread_t owner;
    tree *model, *working, *presentation, *save[PHASE26D_MAX_TX_DEPTH];
    uint32_t depth, generations[PHASE26D_MAX_LAYERS];
    uint64_t commits, mutations, frames;
    size_t image_bytes, image_count;
};
typedef struct { double a,b,c,d,tx,ty; } matrix;
typedef struct { matrix m; int width,height,radius; } clip;

static bool finite_value(double x) { return isfinite(x) != 0; }
static bool can_write(const phase26d_context *c) {
    return c->depth && pthread_equal(c->owner,pthread_self());
}
static void image_retain(image *im) { if (im) ++im->refs; }
static void image_release(phase26d_context *c, image *im) {
    if (im && --im->refs == 0) {
        c->image_bytes -= im->bytes;
        --c->image_count;
        free(im);
    }
}
static void clear_tree(phase26d_context *c, tree *t) {
    for (unsigned i=0;i<PHASE26D_MAX_LAYERS;i++) {
        image_release(c,t->l[i].backing);
        t->l[i].backing=NULL;
    }
}
static void assign_tree(phase26d_context *c, tree *dest, const tree *src) {
    if (dest==src) return;
    clear_tree(c,dest);
    memcpy(dest,src,sizeof(*dest));
    for (unsigned i=0;i<PHASE26D_MAX_LAYERS;i++) image_retain(dest->l[i].backing);
}
static layer *find(tree *t, phase26d_id id) {
    if (!id.generation || id.slot>=PHASE26D_MAX_LAYERS) return NULL;
    layer *l=&t->l[id.slot];
    return l->used && l->generation==id.generation ? l : NULL;
}
static bool desc_ok(const phase26d_layer_desc *d) {
    if (!d || d->width<0 || d->height<0 || d->width>4096 || d->height>4096 ||
        d->x < -100000 || d->x > 100000 || d->y < -100000 || d->y > 100000 ||
        d->corner_radius>128 || d->blur_radius>3 || d->shadow_radius>8 ||
        d->shadow_x < -32 || d->shadow_x > 32 || d->shadow_y < -32 || d->shadow_y > 32 ||
        d->clips>1 || d->material>1) return false;
    const phase26d_affine *m=&d->transform;
    double v[]={m->a,m->b,m->c,m->d,m->tx,m->ty};
    for (unsigned i=0;i<6;i++) if (!finite_value(v[i]) || fabs(v[i])>100000) return false;
    double det=m->a*m->d-m->b*m->c;
    return finite_value(det) && fabs(det)>=0.000001 && fabs(det)<=1000000;
}
phase26d_layer_desc phase26d_default_desc(void) {
    phase26d_layer_desc d={0};
    d.width=1; d.height=1; d.opacity=255;
    d.transform.a=1; d.transform.d=1;
    d.rgba[3]=255;
    return d;
}
phase26d_context *phase26d_new(void) {
    phase26d_context *c=calloc(1,sizeof(*c));
    if (!c) return NULL;
    if (pthread_mutex_init(&c->mutex,NULL)!=0) { free(c); return NULL; }
    c->model=calloc(1,sizeof(tree));
    c->working=calloc(1,sizeof(tree));
    c->presentation=calloc(1,sizeof(tree));
    for (unsigned i=0;i<PHASE26D_MAX_TX_DEPTH;i++) c->save[i]=calloc(1,sizeof(tree));
    if (!c->model || !c->working || !c->presentation) { phase26d_free(c); return NULL; }
    for (unsigned i=0;i<PHASE26D_MAX_TX_DEPTH;i++)
        if (!c->save[i]) { phase26d_free(c); return NULL; }
    return c;
}
void phase26d_free(phase26d_context *c) {
    if (!c) return;
    pthread_mutex_lock(&c->mutex);
    if (c->model) clear_tree(c,c->model);
    if (c->working) clear_tree(c,c->working);
    if (c->presentation) clear_tree(c,c->presentation);
    for (unsigned i=0;i<PHASE26D_MAX_TX_DEPTH;i++)
        if (c->save[i]) clear_tree(c,c->save[i]);
    free(c->model); free(c->working); free(c->presentation);
    for (unsigned i=0;i<PHASE26D_MAX_TX_DEPTH;i++) free(c->save[i]);
    pthread_mutex_unlock(&c->mutex);
    pthread_mutex_destroy(&c->mutex);
    free(c);
}
phase26d_result phase26d_begin(phase26d_context *c) {
    if (!c) return PHASE26D_INVALID;
    pthread_mutex_lock(&c->mutex);
    if (c->depth && !can_write(c)) { pthread_mutex_unlock(&c->mutex); return PHASE26D_STATE; }
    if (c->depth==PHASE26D_MAX_TX_DEPTH) { pthread_mutex_unlock(&c->mutex); return PHASE26D_LIMIT; }
    if (!c->depth) { c->owner=pthread_self(); assign_tree(c,c->working,c->model); }
    assign_tree(c,c->save[c->depth],c->working);
    ++c->depth;
    pthread_mutex_unlock(&c->mutex);
    return PHASE26D_OK;
}
phase26d_result phase26d_commit(phase26d_context *c) {
    if (!c) return PHASE26D_INVALID;
    pthread_mutex_lock(&c->mutex);
    if (!can_write(c)) { pthread_mutex_unlock(&c->mutex); return PHASE26D_STATE; }
    --c->depth;
    if (!c->depth) {
        assign_tree(c,c->model,c->working);
        assign_tree(c,c->presentation,c->model);
        ++c->commits;
    }
    pthread_mutex_unlock(&c->mutex);
    return PHASE26D_OK;
}
phase26d_result phase26d_rollback(phase26d_context *c) {
    if (!c) return PHASE26D_INVALID;
    pthread_mutex_lock(&c->mutex);
    if (!can_write(c)) { pthread_mutex_unlock(&c->mutex); return PHASE26D_STATE; }
    --c->depth;
    assign_tree(c,c->working,c->save[c->depth]);
    pthread_mutex_unlock(&c->mutex);
    return PHASE26D_OK;
}
phase26d_result phase26d_create(phase26d_context *c, const phase26d_layer_desc *d, phase26d_id *out) {
    if (!c || !out || !desc_ok(d)) return PHASE26D_INVALID;
    pthread_mutex_lock(&c->mutex);
    if (!can_write(c)) { pthread_mutex_unlock(&c->mutex); return PHASE26D_STATE; }
    for (unsigned i=0;i<PHASE26D_MAX_LAYERS;i++) if (!c->working->l[i].used) {
        layer *l=&c->working->l[i];
        uint32_t g=++c->generations[i];
        if (!g) g=++c->generations[i];
        memset(l,0,sizeof(*l));
        l->used=true; l->generation=g; l->d=*d;
        *out=(phase26d_id){i,g};
        ++c->mutations;
        pthread_mutex_unlock(&c->mutex);
        return PHASE26D_OK;
    }
    pthread_mutex_unlock(&c->mutex);
    return PHASE26D_LIMIT;
}
phase26d_result phase26d_set(phase26d_context *c, phase26d_id id, const phase26d_layer_desc *d) {
    if (!c || !desc_ok(d)) return PHASE26D_INVALID;
    pthread_mutex_lock(&c->mutex);
    layer *l=can_write(c) ? find(c->working,id):NULL;
    if (l) { l->d=*d; ++c->mutations; }
    pthread_mutex_unlock(&c->mutex);
    return l ? PHASE26D_OK : (can_write(c) ? PHASE26D_STALE:PHASE26D_STATE);
}
phase26d_result phase26d_parent(phase26d_context *c, phase26d_id id, phase26d_id parent) {
    if (!c) return PHASE26D_INVALID;
    pthread_mutex_lock(&c->mutex);
    layer *l=can_write(c) ? find(c->working,id):NULL;
    if (!l) { phase26d_result r=can_write(c)?PHASE26D_STALE:PHASE26D_STATE;
        pthread_mutex_unlock(&c->mutex); return r; }
    if (parent.generation) {
        layer *p=find(c->working,parent);
        if (!p) { pthread_mutex_unlock(&c->mutex); return PHASE26D_STALE; }
        phase26d_id cursor=parent;
        for (unsigned n=0;n<PHASE26D_MAX_LAYERS;n++) {
            if (cursor.slot==id.slot && cursor.generation==id.generation) {
                pthread_mutex_unlock(&c->mutex); return PHASE26D_INVALID;
            }
            layer *step=find(c->working,cursor);
            if (!step || !step->parent.generation) break;
            cursor=step->parent;
            if (n==PHASE26D_MAX_LAYERS-1) { pthread_mutex_unlock(&c->mutex); return PHASE26D_INVALID; }
        }
    }
    l->parent=parent.generation ? parent:(phase26d_id){0,0};
    ++c->mutations;
    pthread_mutex_unlock(&c->mutex);
    return PHASE26D_OK;
}
phase26d_result phase26d_destroy(phase26d_context *c, phase26d_id id) {
    if (!c) return PHASE26D_INVALID;
    pthread_mutex_lock(&c->mutex);
    layer *l=can_write(c) ? find(c->working,id):NULL;
    if (!l) { phase26d_result r=can_write(c)?PHASE26D_STALE:PHASE26D_STATE;
        pthread_mutex_unlock(&c->mutex); return r; }
    /* Cascade deletion until no descendants remain. All generations stay retired. */
    bool marked[PHASE26D_MAX_LAYERS]={false};
    marked[id.slot]=true;
    for (unsigned pass=0;pass<PHASE26D_MAX_LAYERS;pass++) {
        bool changed=false;
        for (unsigned i=0;i<PHASE26D_MAX_LAYERS;i++) {
            layer *child=&c->working->l[i];
            if (child->used && !marked[i] && child->parent.generation &&
                child->parent.slot<PHASE26D_MAX_LAYERS && marked[child->parent.slot]) {
                marked[i]=true; changed=true;
            }
        }
        if (!changed) break;
    }
    for (unsigned i=0;i<PHASE26D_MAX_LAYERS;i++) if (marked[i]) {
        image_release(c,c->working->l[i].backing);
        memset(&c->working->l[i],0,sizeof(layer));
        ++c->mutations;
    }
    pthread_mutex_unlock(&c->mutex);
    return PHASE26D_OK;
}
phase26d_result phase26d_upload(phase26d_context *c, phase26d_id id, const uint8_t *pixels,
                                 uint32_t w,uint32_t h,uint32_t stride,size_t source_bytes) {
    if (!c || !pixels || !w || !h || w>PHASE26D_MAX_IMAGE_DIM || h>PHASE26D_MAX_IMAGE_DIM ||
        stride<w*4u || stride>1048576u ||
        source_bytes < (size_t)(h-1u)*stride+(size_t)w*4u) return PHASE26D_INVALID;
    size_t bytes=(size_t)w*h*4u;
    if (bytes>PHASE26D_MAX_IMAGE_BYTES) return PHASE26D_LIMIT;
    pthread_mutex_lock(&c->mutex);
    layer *l=can_write(c)?find(c->working,id):NULL;
    if (!l) { phase26d_result r=can_write(c)?PHASE26D_STALE:PHASE26D_STATE;
        pthread_mutex_unlock(&c->mutex); return r; }
    if (c->image_bytes > PHASE26D_IMAGE_BUDGET-bytes) {
        pthread_mutex_unlock(&c->mutex); return PHASE26D_LIMIT;
    }
    image *im=malloc(sizeof(*im)+bytes);
    if (!im) { pthread_mutex_unlock(&c->mutex); return PHASE26D_MEMORY; }
    im->refs=1; im->width=w; im->height=h; im->bytes=bytes;
    for (uint32_t y=0;y<h;y++) {
        const uint8_t *row=pixels+(size_t)y*stride;
        for (uint32_t x=0;x<w;x++) {
            const uint8_t *p=row+x*4u;
            if (p[0]>p[3] || p[1]>p[3] || p[2]>p[3]) {
                free(im); pthread_mutex_unlock(&c->mutex); return PHASE26D_INVALID;
            }
        }
        memcpy(im->pixels+(size_t)y*w*4u,row,(size_t)w*4u);
    }
    c->image_bytes+=bytes; ++c->image_count;
    image_release(c,l->backing);
    l->backing=im;
    ++c->mutations;
    pthread_mutex_unlock(&c->mutex);
    return PHASE26D_OK;
}
phase26d_result phase26d_animate(phase26d_context *c,phase26d_id id,phase26d_anim_kind kind,
                                  double from,double to,uint64_t start,uint64_t duration) {
    if (!c || kind<PHASE26D_ANIM_OPACITY || kind>PHASE26D_ANIM_Y ||
        !finite_value(from) || !finite_value(to) || !duration || UINT64_MAX-start<duration)
        return PHASE26D_INVALID;
    if (kind==PHASE26D_ANIM_OPACITY ? (from<0 || to<0 || from>255 || to>255):
        (fabs(from)>100000 || fabs(to)>100000)) return PHASE26D_INVALID;
    pthread_mutex_lock(&c->mutex);
    layer *l=can_write(c)?find(c->working,id):NULL;
    if (l) {
        animation *a=&l->anim[(unsigned)kind-1u];
        *a=(animation){true,kind,from,to,start,duration}; ++c->mutations;
    }
    pthread_mutex_unlock(&c->mutex);
    return l ? PHASE26D_OK : (can_write(c)?PHASE26D_STALE:PHASE26D_STATE);
}
phase26d_result phase26d_cancel(phase26d_context *c,phase26d_id id,phase26d_anim_kind kind) {
    if (!c || kind<PHASE26D_ANIM_OPACITY || kind>PHASE26D_ANIM_Y) return PHASE26D_INVALID;
    pthread_mutex_lock(&c->mutex);
    layer *l=can_write(c)?find(c->working,id):NULL;
    if (l) { l->anim[(unsigned)kind-1u].active=false; ++c->mutations; }
    pthread_mutex_unlock(&c->mutex);
    return l ? PHASE26D_OK : (can_write(c)?PHASE26D_STALE:PHASE26D_STATE);
}
phase26d_result phase26d_step(phase26d_context *c,uint64_t now) {
    if (!c) return PHASE26D_INVALID;
    pthread_mutex_lock(&c->mutex);
    assign_tree(c,c->presentation,c->model);
    for (unsigned i=0;i<PHASE26D_MAX_LAYERS;i++) {
        layer *l=&c->presentation->l[i];
        if (!l->used) continue;
        for (unsigned j=0;j<3;j++) {
            const animation *a=&l->anim[j];
            if (!a->active) continue;
            double t=now<=a->start?0.0:(now>=a->start+a->duration?1.0:
                (double)(now-a->start)/(double)a->duration);
            double val=a->from+(a->to-a->from)*t;
            if (j==0) l->d.opacity=(uint8_t)lround(val);
            else if (j==1) l->d.x=(int32_t)lround(val);
            else l->d.y=(int32_t)lround(val);
        }
    }
    ++c->frames;
    pthread_mutex_unlock(&c->mutex);
    return PHASE26D_OK;
}
phase26d_result phase26d_get(phase26d_context *c,phase26d_id id,int presentation,phase26d_layer_desc *out) {
    if (!c || !out || (presentation!=0 && presentation!=1)) return PHASE26D_INVALID;
    pthread_mutex_lock(&c->mutex);
    layer *l=find(presentation?c->presentation:c->model,id);
    if (l) *out=l->d;
    pthread_mutex_unlock(&c->mutex);
    return l?PHASE26D_OK:PHASE26D_STALE;
}
phase26d_result phase26d_stats_read(phase26d_context *c,phase26d_stats *out) {
    if (!c || !out) return PHASE26D_INVALID;
    pthread_mutex_lock(&c->mutex);
    *out=(phase26d_stats){c->commits,c->mutations,c->frames,
        c->image_bytes,c->image_count,0,0,c->depth};
    for (unsigned i=0;i<PHASE26D_MAX_LAYERS;i++) {
        out->model_layers+=(uint32_t)c->model->l[i].used;
        out->presented_layers+=(uint32_t)c->presentation->l[i].used;
    }
    pthread_mutex_unlock(&c->mutex);
    return PHASE26D_OK;
}
static matrix mul(matrix p,matrix q) {
    return (matrix){p.a*q.a+p.c*q.b,p.b*q.a+p.d*q.b,
        p.a*q.c+p.c*q.d,p.b*q.c+p.d*q.d,
        p.a*q.tx+p.c*q.ty+p.tx,p.b*q.tx+p.d*q.ty+p.ty};
}
static matrix layer_matrix(matrix parent,const phase26d_layer_desc *d) {
    matrix position={1,0,0,1,(double)d->x,(double)d->y};
    const phase26d_affine *a=&d->transform;
    matrix own={a->a,a->b,a->c,a->d,a->tx,a->ty};
    return mul(mul(parent,position),own);
}
static bool local(matrix m,double x,double y,double *ox,double *oy) {
    double det=m.a*m.d-m.b*m.c;
    if (!finite_value(det) || fabs(det)<0.000001) return false;
    x-=m.tx; y-=m.ty;
    *ox=(m.d*x-m.c*y)/det; *oy=(-m.b*x+m.a*y)/det;
    return finite_value(*ox) && finite_value(*oy);
}
static bool rounded(double x,double y,int w,int h,int r) {
    if (x<0 || y<0 || x>=w || y>=h) return false;
    if (!r) return true;
    double cr=fmin((double)r,fmin((double)w,(double)h)/2.0);
    double px=fmax(cr,fmin(x,(double)w-cr));
    double py=fmax(cr,fmin(y,(double)h-cr));
    double dx=x-px,dy=y-py;
    return dx*dx+dy*dy<=cr*cr;
}
static unsigned scale(unsigned c,unsigned a) { return (c*a+127u)/255u; }
static void over(uint8_t *dst,const uint8_t src[4],unsigned opacity) {
    unsigned alpha=scale(src[3],opacity);
    unsigned inv=255u-alpha;
    for (unsigned k=0;k<3;k++) {
        unsigned v=scale(src[k],opacity)+scale(dst[k],inv);
        dst[k]=(uint8_t)(v>255u?255u:v);
    }
    unsigned v=alpha+scale(dst[3],inv);
    dst[3]=(uint8_t)(v>255u?255u:v);
}
static void sample(const layer *l,double x,double y,uint8_t out[4]) {
    const phase26d_layer_desc *d=&l->d;
    if (d->material) {
        const uint8_t material[4]={24,24,24,80};
        memcpy(out,material,4);
    } else {
        unsigned a=d->rgba[3];
        out[0]=(uint8_t)scale(d->rgba[0],a);
        out[1]=(uint8_t)scale(d->rgba[1],a);
        out[2]=(uint8_t)scale(d->rgba[2],a);
        out[3]=(uint8_t)a;
    }
    if (!l->backing) return;
    const image *im=l->backing;
    int xi=(int)floor(x), yi=(int)floor(y);
    if (xi<0 || yi<0 || (unsigned)xi>=im->width || (unsigned)yi>=im->height) return;
    unsigned r=d->blur_radius, count=0, accum[4]={0};
    for (int by=-(int)r;by<=(int)r;by++) for (int bx=-(int)r;bx<=(int)r;bx++) {
        int px=xi+bx,py=yi+by;
        if (px<0 || py<0 || (unsigned)px>=im->width || (unsigned)py>=im->height) continue;
        const uint8_t *p=im->pixels+((size_t)py*im->width+(unsigned)px)*4u;
        for (unsigned k=0;k<4;k++) accum[k]+=p[k];
        ++count;
    }
    if (count) {
        uint8_t src[4];
        for (unsigned k=0;k<4;k++) src[k]=(uint8_t)(accum[k]/count);
        over(out,src,255);
    }
}
static bool inside_clips(const clip *cl,unsigned n,double px,double py) {
    for (unsigned i=0;i<n;i++) {
        double x,y;
        if (!local(cl[i].m,px,py,&x,&y) ||
            !rounded(x,y,cl[i].width,cl[i].height,cl[i].radius)) return false;
    }
    return true;
}
static void render_layer(const tree *t,unsigned index,matrix parent,clip *clips,unsigned depth,
                         phase26d_frame f,unsigned opacity,unsigned recursion) {
    if (recursion>=PHASE26D_MAX_LAYERS) return;
    const layer *l=&t->l[index];
    if (!l->used) return;
    const phase26d_layer_desc *d=&l->d;
    matrix m=layer_matrix(parent,d);
    unsigned inherited=scale(opacity,d->opacity);
    if (d->width>0 && d->height>0 && inherited) {
        /* Pixel centers are sampled through the affine inverse. The finite frame
           bound also prevents a giant transformed rectangle from creating an
           unbounded CPU loop. */
        for (uint32_t y=0;y<f.height;y++) for (uint32_t x=0;x<f.width;x++) {
            double px=(double)x+0.5,py=(double)y+0.5,lx,ly;
            if (!inside_clips(clips,depth,px,py) || !local(m,px,py,&lx,&ly)) continue;
            uint8_t *dst=f.pixels+(size_t)y*f.stride+(size_t)x*4u;
            if (d->shadow_alpha) {
                double sx=lx-d->shadow_x,sy=ly-d->shadow_y;
                double dx=fmax(0.0,fmax(-sx,sx-d->width));
                double dy=fmax(0.0,fmax(-sy,sy-d->height));
                double distance=fmax(dx,dy);
                if (distance<=d->shadow_radius && !rounded(sx,sy,d->width,d->height,d->corner_radius)) {
                    unsigned a=scale(d->shadow_alpha,255u-(unsigned)(255.0*distance/(d->shadow_radius+1u)));
                    uint8_t shadow[4]={0,0,0,(uint8_t)a};
                    over(dst,shadow,inherited);
                }
            }
            if (rounded(lx,ly,d->width,d->height,d->corner_radius)) {
                uint8_t color[4];
                sample(l,lx,ly,color);
                over(dst,color,inherited);
            }
        }
    }
    if (d->clips) {
        clips[depth]=(clip){m,d->width,d->height,d->corner_radius};
        ++depth;
    }
    /* Stable insertion ordering for equal z, independent of mutation history. */
    bool visited[PHASE26D_MAX_LAYERS]={false};
    for (unsigned n=0;n<PHASE26D_MAX_LAYERS;n++) {
        unsigned best=PHASE26D_MAX_LAYERS;
        for (unsigned j=0;j<PHASE26D_MAX_LAYERS;j++) {
            const layer *ch=&t->l[j];
            if (visited[j] || !ch->used || ch->parent.generation!=l->generation ||
                ch->parent.slot!=index) continue;
            if (best==PHASE26D_MAX_LAYERS || ch->d.z<t->l[best].d.z ||
                (ch->d.z==t->l[best].d.z && j<best)) best=j;
        }
        if (best==PHASE26D_MAX_LAYERS) break;
        visited[best]=true;
        render_layer(t,best,m,clips,depth,f,inherited,recursion+1u);
    }
}
phase26d_result phase26d_render(phase26d_context *c,phase26d_frame f) {
    if (!c || !f.pixels || !f.width || !f.height ||
        f.width>PHASE26D_MAX_FRAME_DIM || f.height>PHASE26D_MAX_FRAME_DIM ||
        f.stride<f.width*4u || f.stride>1048576u ||
        f.capacity < (size_t)(f.height-1u)*f.stride+(size_t)f.width*4u ||
        (size_t)f.stride*f.height>PHASE26D_MAX_IMAGE_BYTES)
        return PHASE26D_INVALID;
    pthread_mutex_lock(&c->mutex);
    for (uint32_t y=0;y<f.height;y++) memset(f.pixels+(size_t)y*f.stride,0,(size_t)f.width*4u);
    matrix identity={1,0,0,1,0,0};
    clip clips[PHASE26D_MAX_LAYERS];
    bool visited[PHASE26D_MAX_LAYERS]={false};
    for (unsigned n=0;n<PHASE26D_MAX_LAYERS;n++) {
        unsigned best=PHASE26D_MAX_LAYERS;
        for (unsigned j=0;j<PHASE26D_MAX_LAYERS;j++) {
            const layer *l=&c->presentation->l[j];
            if (visited[j] || !l->used || l->parent.generation) continue;
            if (best==PHASE26D_MAX_LAYERS || l->d.z<c->presentation->l[best].d.z ||
                (l->d.z==c->presentation->l[best].d.z && j<best)) best=j;
        }
        if (best==PHASE26D_MAX_LAYERS) break;
        visited[best]=true;
        render_layer(c->presentation,best,identity,clips,0,f,255,0);
    }
    pthread_mutex_unlock(&c->mutex);
    return PHASE26D_OK;
}
