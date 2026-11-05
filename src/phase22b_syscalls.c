#include "ios27/phase22b_syscalls.h"
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define B22_THREADS 64u
#define B22_NAME 48u
#define B22_NS UINT64_C(1000000000)
typedef struct {
    char name[B22_NAME];
    uint8_t bytes[IOS27_22B_CAP_BYTES];
    size_t used;
    uint8_t active, writable;
} b22_cap;
typedef struct {
    uint8_t open, resource, mode, append;
    size_t offset;
} b22_fd;
typedef struct { pthread_t handle; uint64_t id; } b22_thread;
struct ios27_22b_context {
    pthread_mutex_t mutex;
    ios27_20e_ctx *guest; /* borrowed, never freed by this adapter */
    uint32_t pid, uid, gid;
    int guest_errno;
    b22_cap caps[IOS27_22B_CAPS];
    b22_fd fds[IOS27_22B_FDS];
    uint8_t stdio[3][IOS27_22B_CAP_BYTES];
    size_t std_len[3];
    uint64_t real_ns, mono_ns;
    uint8_t interrupted;
    b22_thread threads[B22_THREADS];
    unsigned thread_count;
};
static ios27_22b_result ok(int64_t v) {
    ios27_22b_result r = {v, 0, 0}; return r;
}
static ios27_22b_result fail(ios27_22b_context *c, int e) {
    ios27_22b_result r = {e, 1, e}; if (c) c->guest_errno=e; return r;
}
static int valid_name(const char *s) {
    size_t n;
    if (!s) return 0;
    for (n=0;n<B22_NAME && s[n];++n) {
        unsigned char ch=(unsigned char)s[n];
        if (!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||
              (ch>='0'&&ch<='9')||ch=='_'||ch=='-')) return 0;
    }
    return n>0 && n<B22_NAME;
}
static uint8_t *guest_at(ios27_22b_context *c, uint64_t addr,
                         uint64_t len, int wr) {
    /* Existing phase20e boundary owns mapping and per-page access checks. */
    return (uint8_t *)ios27_20e_guest_ptr(c->guest,addr,len,wr);
}
int ios27_22b_create(ios27_22b_context **out, ios27_20e_ctx *guest,
                     uint32_t pid, uint32_t uid, uint32_t gid) {
    ios27_22b_context *c;
    if (!out || !guest || !pid) return -1;
    *out=NULL;
    c=(ios27_22b_context *)calloc(1,sizeof(*c)); if (!c) return -1;
    if (pthread_mutex_init(&c->mutex,NULL)!=0) { free(c);return -1; }
    c->guest=guest;c->pid=pid;c->uid=uid;c->gid=gid;
    for (size_t i=0;i<3u;++i) {
        c->fds[i].open=1;c->fds[i].resource=(uint8_t)i;
        c->fds[i].mode=(uint8_t)(i==0u?1u:2u);
    }
    *out=c;return 0;
}
void ios27_22b_destroy(ios27_22b_context *c) {
    if (!c)return;
    (void)pthread_mutex_destroy(&c->mutex);
    /* Destroying with active callers is forbidden; join first. */
    memset(c,0,sizeof(*c));free(c);
}
int ios27_22b_preopen(ios27_22b_context *c,const char *name,
                      const void *data,size_t len,int writable) {
    int rc=-1;
    if (!c || !valid_name(name) || len>IOS27_22B_CAP_BYTES || (len&&!data) ||
        (writable!=0 && writable!=1)) return -1;
    pthread_mutex_lock(&c->mutex);
    for(size_t i=0;i<IOS27_22B_CAPS;++i)
        if(c->caps[i].active && strcmp(c->caps[i].name,name)==0)goto done;
    for(size_t i=0;i<IOS27_22B_CAPS;++i)if(!c->caps[i].active) {
        b22_cap *p=&c->caps[i];
        memcpy(p->name,name,strlen(name)+1);
        if(len)memcpy(p->bytes,data,len);
        p->used=len;p->writable=(uint8_t)writable;p->active=1;rc=0;break;
    }
done:
    pthread_mutex_unlock(&c->mutex);return rc;
}
int ios27_22b_stdin(ios27_22b_context *c,const void *data,size_t len) {
    if(!c || len>IOS27_22B_CAP_BYTES || (len&&!data))return -1;
    pthread_mutex_lock(&c->mutex);
    if(len)memcpy(c->stdio[0],data,len);
    c->std_len[0]=len;c->fds[0].offset=0;
    pthread_mutex_unlock(&c->mutex);return 0;
}
size_t ios27_22b_capture(ios27_22b_context *c,int fd,void *out,size_t cap) {
    size_t n=0;
    if(!c || (fd!=1&&fd!=2) || (cap&&!out))return 0;
    pthread_mutex_lock(&c->mutex);n=c->std_len[fd];if(n>cap)n=cap;
    if(n)memcpy(out,c->stdio[fd],n);
    pthread_mutex_unlock(&c->mutex);return n;
}
int ios27_22b_set_clock(ios27_22b_context *c,uint64_t realtime,uint64_t monotonic) {
    if(!c || realtime/B22_NS>(uint64_t)INT64_MAX || monotonic/B22_NS>(uint64_t)INT64_MAX)return -1;
    pthread_mutex_lock(&c->mutex);c->real_ns=realtime;c->mono_ns=monotonic;
    pthread_mutex_unlock(&c->mutex);return 0;
}
int ios27_22b_advance(ios27_22b_context *c,uint64_t n) {
    int rc=-1;
    if(!c)return -1;
    pthread_mutex_lock(&c->mutex);
    if(n<=UINT64_MAX-c->real_ns && n<=UINT64_MAX-c->mono_ns &&
       (c->real_ns+n)/B22_NS<=(uint64_t)INT64_MAX &&
       (c->mono_ns+n)/B22_NS<=(uint64_t)INT64_MAX) {
        c->real_ns+=n;c->mono_ns+=n;rc=0;
    }
    pthread_mutex_unlock(&c->mutex);return rc;
}
void ios27_22b_interrupt(ios27_22b_context *c) {
    if(!c)return;
    pthread_mutex_lock(&c->mutex);c->interrupted=1;
    pthread_mutex_unlock(&c->mutex);
}
int ios27_22b_errno(ios27_22b_context *c) {
    int e;if(!c)return IOS27_22B_EINVAL;
    pthread_mutex_lock(&c->mutex);e=c->guest_errno;
    pthread_mutex_unlock(&c->mutex);return e;
}
int ios27_22b_metadata_fd(ios27_22b_context *c,int fd,
                          ios27_22b_metadata *out) {
    int rc=-1;
    if(!c||!out)return -1;
    pthread_mutex_lock(&c->mutex);
    if(fd>=0 && fd<(int)IOS27_22B_FDS && c->fds[fd].open) {
        b22_fd *d=&c->fds[fd];
        out->size=d->resource<3 ? c->std_len[d->resource] : c->caps[d->resource-3].used;
        out->position=d->offset;out->mode=d->mode;out->resource_id=d->resource;
        rc=0;
    }
    pthread_mutex_unlock(&c->mutex);return rc;
}
static ios27_22b_result open_cap(ios27_22b_context *c,uint64_t name,uint64_t flags) {
    char tmp[sizeof("/cap/")+B22_NAME];size_t i,slot,resource;
    if(flags & ~(uint64_t)(3u|IOS27_22B_O_APPEND))return fail(c,IOS27_22B_EINVAL);
    if((flags&3u)==3u)return fail(c,IOS27_22B_EINVAL);
    for(i=0;i<sizeof(tmp);++i) {
        uint8_t *p;
        if (name>UINT64_MAX-i || !(p=guest_at(c,name+i,1,0)))return fail(c,IOS27_22B_EFAULT);
        tmp[i]=(char)*p;if(!*p)break;
    }
    if(i==sizeof(tmp))return fail(c,IOS27_22B_EINVAL);
    if(strncmp(tmp,"/cap/",5)!=0 || !valid_name(tmp+5))return fail(c,IOS27_22B_EACCES);
    for(slot=3;slot<IOS27_22B_FDS && c->fds[slot].open;++slot){}
    if(slot==IOS27_22B_FDS)return fail(c,IOS27_22B_EMFILE);
    for(resource=0;resource<IOS27_22B_CAPS;++resource)
        if(c->caps[resource].active && strcmp(c->caps[resource].name,tmp+5)==0)break;
    if(resource==IOS27_22B_CAPS)return fail(c,IOS27_22B_ENOENT);
    if((flags&3u)!=IOS27_22B_O_RDONLY && !c->caps[resource].writable)return fail(c,IOS27_22B_EACCES);
    c->fds[slot].open=1;
    c->fds[slot].resource=(uint8_t)(resource+3u);
    c->fds[slot].mode=(uint8_t)((flags&3u)==0u?1u:((flags&3u)==1u?2u:3u));
    c->fds[slot].append=(uint8_t)((flags&IOS27_22B_O_APPEND)!=0);
    c->fds[slot].offset=0;
    return ok((int64_t)slot);
}
static ios27_22b_result io_vector(ios27_22b_context *c,int writing,
                                  uint64_t rawfd,uint64_t p,uint64_t count,
                                  int vector) {
    struct slice { uint64_t address, size; } slices[IOS27_22B_IOV_MAX];
    uint8_t *base;size_t used,pos,capacity,total=0,nparts=1;
    b22_fd *d;uint8_t *storage;size_t *plen;
    if(rawfd>=IOS27_22B_FDS || !c->fds[rawfd].open)return fail(c,IOS27_22B_EBADF);
    d=&c->fds[rawfd];
    if(writing ? !(d->mode&2u) : !(d->mode&1u))return fail(c,IOS27_22B_EBADF);
    if(vector) {
        if(count>IOS27_22B_IOV_MAX)return fail(c,IOS27_22B_EINVAL);
        nparts=(size_t)count;
        if(nparts) {
            base=guest_at(c,p,(uint64_t)(nparts*16u),0);
            if(!base || (p&7u))return fail(c,IOS27_22B_EFAULT);
            for(size_t i=0;i<nparts;++i) {
                memcpy(&slices[i].address,base+i*16u,8);
                memcpy(&slices[i].size,base+i*16u+8,8);
            }
        }
    } else { slices[0].address=p;slices[0].size=count; }
    for(size_t i=0;i<nparts;++i) {
        if(slices[i].size>IOS27_22B_CAP_BYTES-total)return fail(c,IOS27_22B_EINVAL);
        if(slices[i].size && !guest_at(c,slices[i].address,slices[i].size,!writing))
            return fail(c,IOS27_22B_EFAULT);
        total+=(size_t)slices[i].size;
    }
    if(d->resource<3u){storage=c->stdio[d->resource];plen=&c->std_len[d->resource];}
    else {b22_cap *r=&c->caps[d->resource-3u];storage=r->bytes;plen=&r->used;}
    capacity=IOS27_22B_CAP_BYTES;used=*plen;
    pos=(writing && (d->append || d->resource==1u || d->resource==2u))?used:d->offset;
    if(writing) {
        if(pos>capacity || total>capacity-pos)return fail(c,IOS27_22B_ENOSPC);
        if(pos>used)memset(storage+used,0,pos-used);
        for(size_t i=0;i<nparts;++i)if(slices[i].size) {
            memcpy(storage+pos,guest_at(c,slices[i].address,slices[i].size,0),(size_t)slices[i].size);
            pos+=(size_t)slices[i].size;
        }
        if(pos>*plen)*plen=pos;
        d->offset=pos;
        return ok((int64_t)total);
    }
    /* Read is bounded by available data. Full guest range validated before
       consuming any descriptor offset or touching any destination. */
    if(pos>=used)total=0;
    else if(total>used-pos)total=used-pos;
    size_t left=total;
    for(size_t i=0;i<nparts && left;++i) {
        size_t n=(size_t)slices[i].size;if(n>left)n=left;
        if(n)memcpy(guest_at(c,slices[i].address,n,1),storage+pos,n);
        pos+=n;left-=n;
    }
    if(total)d->offset=pos;return ok((int64_t)total);
}
static ios27_22b_result seek_fd(ios27_22b_context *c,uint64_t rawfd,uint64_t delta,uint64_t whence) {
    int64_t shift,base,now; b22_fd *d;
    if(rawfd<3u || rawfd>=IOS27_22B_FDS || !c->fds[rawfd].open)return fail(c,IOS27_22B_EBADF);
    d=&c->fds[rawfd];
    memcpy(&shift,&delta,sizeof(shift));
    if(whence==0u)base=0;
    else if(whence==1u)base=(int64_t)d->offset;
    else if(whence==2u)base=(int64_t)c->caps[d->resource-3u].used;
    else return fail(c,IOS27_22B_EINVAL);
    if((shift>0 && shift>INT64_MAX-base)||(shift<0 && shift< -base))return fail(c,IOS27_22B_EINVAL);
    now=base+shift;
    if(now<0 || now>(int64_t)IOS27_22B_CAP_BYTES)return fail(c,IOS27_22B_EINVAL);
    d->offset=(size_t)now;return ok(now);
}
static ios27_22b_result clock_ns(ios27_22b_context *c,uint64_t clk,uint64_t dest) {
    uint64_t time;int64_t sec,nsec;uint8_t *ptr;
    if(clk==IOS27_22B_CLOCK_REALTIME)time=c->real_ns;
    else if(clk==IOS27_22B_CLOCK_MONOTONIC)time=c->mono_ns;
    else return fail(c,IOS27_22B_EINVAL);
    if(!(ptr=guest_at(c,dest,16u,1)) || (dest&7u))return fail(c,IOS27_22B_EFAULT);
    sec=(int64_t)(time/B22_NS);nsec=(int64_t)(time%B22_NS);
    memcpy(ptr,&sec,8);memcpy(ptr+8,&nsec,8);return ok(0);
}
static ios27_22b_result dispatch_locked(ios27_22b_context *c,uint32_t trap,const uint64_t a[6]) {
    uint64_t n;
    if((trap&UINT32_C(0xffff0000))==IOS27_22B_PRIVATE) {
        switch(trap&UINT32_C(0xffff)) {
            case IOS27_22B_CLOCK_GETTIME:return clock_ns(c,a[0],a[1]);
            case IOS27_22B_NANOSLEEP: goto virtual_sleep;
            default:return fail(c,IOS27_22B_ENOSYS);
        }
    }
    if((trap&UINT32_C(0xffff0000))!=IOS27_22B_BSD)return fail(c,IOS27_22B_ENOSYS);
    n=(uint64_t)(trap&UINT32_C(0xffff));
    switch(n) {
    case IOS27_22B_READ:return io_vector(c,0,a[0],a[1],a[2],0);
    case IOS27_22B_WRITE:return io_vector(c,1,a[0],a[1],a[2],0);
    case IOS27_22B_READV:return io_vector(c,0,a[0],a[1],a[2],1);
    case IOS27_22B_WRITEV:return io_vector(c,1,a[0],a[1],a[2],1);
    case IOS27_22B_OPEN:return open_cap(c,a[0],a[1]);
    case IOS27_22B_CLOSE:
        if(a[0]>=IOS27_22B_FDS || !c->fds[a[0]].open)return fail(c,IOS27_22B_EBADF);
        memset(&c->fds[a[0]],0,sizeof(c->fds[a[0]]));return ok(0);
    case IOS27_22B_LSEEK:return seek_fd(c,a[0],a[1],a[2]);
    case IOS27_22B_GETPID:return ok(c->pid);
    case IOS27_22B_GETUID:case IOS27_22B_GETEUID:return ok(c->uid);
    case IOS27_22B_GETGID:case IOS27_22B_GETEGID:return ok(c->gid);
    case IOS27_22B_GETTIMEOFDAY: {
        uint64_t microsec=c->real_ns% B22_NS / 1000u;
        int64_t sec=(int64_t)(c->real_ns/B22_NS),usec=(int64_t)microsec;
        uint8_t *ptr;
        if(a[1] || a[2])return fail(c,IOS27_22B_ENOSYS); /* timezone / mach abs not modeled */
        if(!(ptr=guest_at(c,a[0],16u,1)) || (a[0]&7u))return fail(c,IOS27_22B_EFAULT);
        memcpy(ptr,&sec,8);memcpy(ptr+8,&usec,8);return ok(0);
    }
    case IOS27_22B_THREAD_SELFID: {
        pthread_t self=pthread_self();
        for(unsigned i=0;i<c->thread_count;++i)
            if(pthread_equal(c->threads[i].handle,self))return ok((int64_t)c->threads[i].id);
        if(c->thread_count>=B22_THREADS)return fail(c,IOS27_22B_EAGAIN);
        c->threads[c->thread_count].handle=self;
        c->threads[c->thread_count].id=UINT64_C(1000)+c->thread_count+1u;
        return ok((int64_t)c->threads[c->thread_count++].id);
    }
    default:return fail(c,IOS27_22B_ENOSYS);
    }
virtual_sleep: {
        int64_t sec,ns;uint64_t duration;uint8_t *req,*remaining=NULL;
        if(!(req=guest_at(c,a[0],16u,0)) || (a[0]&7u))return fail(c,IOS27_22B_EFAULT);
        memcpy(&sec,req,8);memcpy(&ns,req+8,8);
        if(sec<0 || ns<0 || ns>=(int64_t)B22_NS || (uint64_t)sec>UINT64_MAX/B22_NS)
            return fail(c,IOS27_22B_EINVAL);
        if((uint64_t)ns>UINT64_MAX-(uint64_t)sec*B22_NS)
            return fail(c,IOS27_22B_EOVERFLOW);
        duration=(uint64_t)sec*B22_NS+(uint64_t)ns;
        if(a[1] && (!(remaining=guest_at(c,a[1],16u,1))||(a[1]&7u)))
            return fail(c,IOS27_22B_EFAULT);
        if(c->interrupted) {
            c->interrupted=0;
            if(remaining)memmove(remaining,req,16);
            return fail(c,IOS27_22B_EINTR);
        }
        if(duration>UINT64_MAX-c->real_ns || duration>UINT64_MAX-c->mono_ns ||
           (c->real_ns+duration)/B22_NS>(uint64_t)INT64_MAX ||
           (c->mono_ns+duration)/B22_NS>(uint64_t)INT64_MAX)
            return fail(c,IOS27_22B_EOVERFLOW);
        c->real_ns+=duration;c->mono_ns+=duration;
        if(remaining)memset(remaining,0,16u);
        return ok(0); /* virtual clock, does not block host thread */
    }
}
ios27_22b_result ios27_22b_dispatch(ios27_22b_context *c,uint32_t trap,const uint64_t a[6]) {
    ios27_22b_result r;
    if(!c || !a)return fail(NULL,IOS27_22B_EINVAL);
    pthread_mutex_lock(&c->mutex);r=dispatch_locked(c,trap,a);
    pthread_mutex_unlock(&c->mutex);return r;
}