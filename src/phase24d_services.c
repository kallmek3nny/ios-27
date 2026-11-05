#define _POSIX_C_SOURCE 200809L
#include "ios27/phase24d_services.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PAGES (IOS27_24D_MEMORY / IOS27_24D_PAGE)
typedef struct { uint8_t live, writable; char path[64]; uint8_t data[IOS27_24D_FILE_BYTES]; size_t size; } file_t;
typedef struct { uint8_t live, mode; uint16_t refs; uint32_t file; size_t pos; } ofd_t;
typedef struct { uint8_t live, cloexec; uint16_t ofd; } fd_t;
typedef struct {
    uint8_t live, prot[PAGES], memory[IOS27_24D_MEMORY];
    uint32_t generation; file_t files[IOS27_24D_FILES];
    fd_t fds[IOS27_24D_FDS]; ofd_t ofds[IOS27_24D_OFD];
    uint64_t dead[IOS27_24D_PORTS]; uint32_t dead_read, dead_write;
} proc_t;
typedef struct { uint8_t live, cancelled; uint16_t owner; uint32_t generation; uint64_t last_corr; } thread_t;
typedef struct {
    uint8_t live; uint16_t owner; uint64_t name, expected_corr;
    uint8_t rights[IOS27_24D_TASKS], once[IOS27_24D_TASKS];
    ios27_24d_message queue[IOS27_24D_QUEUE];
    unsigned head, used;
} port_t;
struct ios27_24d_runtime {
    pthread_mutex_t lock; pthread_cond_t changed;
    proc_t proc[IOS27_24D_TASKS];
    thread_t threads[IOS27_24D_THREADS];
    port_t ports[IOS27_24D_PORTS];
    uint32_t process_gen, thread_gen; uint64_t next_port;
    ios27_24d_stats stats;
};
_Static_assert(sizeof(ios27_24d_message)==160, "fixed message ABI");
_Static_assert(sizeof(ios27_24d_frame)==56, "fixed register frame ABI");
static uint64_t handle(unsigned slot, uint32_t gen) { return ((uint64_t)gen << 32) | (slot+1u); }
static int find_proc(ios27_24d_runtime *rt, uint64_t pid) {
    unsigned s=(unsigned)(pid & UINT32_MAX);
    if (!s || s>IOS27_24D_TASKS) return -1;
    --s;
    return rt->proc[s].live && handle(s,rt->proc[s].generation)==pid ? (int)s : -1;
}
static int find_thread(ios27_24d_runtime *rt, int task, uint64_t tid) {
    unsigned s=(unsigned)(tid & UINT32_MAX);
    if (!s || s>IOS27_24D_THREADS) return -1;
    --s;
    thread_t *t=&rt->threads[s];
    return t->live && t->owner==(unsigned)task && handle(s,t->generation)==tid ? (int)s : -1;
}
static int find_port(ios27_24d_runtime *rt, uint64_t port) {
    for (unsigned i=0;i<IOS27_24D_PORTS;i++)
        if (rt->ports[i].live && rt->ports[i].name==port) return (int)i;
    return -1;
}
static int span(proc_t *p, uint64_t a, uint64_t size, unsigned perm) {
    if (a==0 || a>IOS27_24D_MEMORY || size>IOS27_24D_MEMORY-a) return 0;
    if (!size) return 1;
    for (uint64_t page=a/IOS27_24D_PAGE;page<=(a+size-1)/IOS27_24D_PAGE;page++)
        if ((p->prot[page]&perm)!=perm) return 0;
    return 1;
}
static int guest_str(proc_t *p, uint64_t addr, char path[64]) {
    for (size_t i=0;i<64;i++) {
        if (addr>IOS27_24D_MEMORY-i || !span(p,addr+i,1,IOS27_24D_READ)) return 0;
        path[i]=(char)p->memory[addr+i];
        if (!path[i]) return i>0;
    }
    return 0;
}
static int fault_or_copy(proc_t *p,uint64_t addr,void *bytes,size_t len,int write) {
    if (!bytes && len) return IOS27_24D_INVALID;
    if (!span(p,addr,len,write?IOS27_24D_WRITE:IOS27_24D_READ)) return IOS27_24D_FAULT;
    if (len) { if (write) memmove(p->memory+addr,bytes,len); else memmove(bytes,p->memory+addr,len); }
    return IOS27_24D_OK;
}
static void close_fd(proc_t *p,unsigned fd) {
    if (!p->fds[fd].live) return;
    ofd_t *o=&p->ofds[p->fds[fd].ofd];
    if (--o->refs==0) memset(o,0,sizeof(*o));
    memset(&p->fds[fd],0,sizeof(p->fds[fd]));
}
static int find_fd(proc_t *p) {
    for (unsigned i=0;i<IOS27_24D_FDS;i++) if (!p->fds[i].live) return (int)i;
    return -1;
}
static int find_ofd(proc_t *p) {
    for (unsigned i=0;i<IOS27_24D_OFD;i++) if (!p->ofds[i].live) return (int)i;
    return -1;
}
static int fd_dispatch(ios27_24d_runtime *rt,proc_t *p,const ios27_24d_frame *f,int64_t *value) {
    unsigned n=f->number;
    if (n==IOS27_24D_SYS_GETPID) return IOS27_24D_UNSUPPORTED;
    rt->stats.fd_ops++;
    if (n==IOS27_24D_SYS_OPEN) {
        if (f->x[1] & ~(uint64_t)(IOS27_24D_OPEN_READ|IOS27_24D_OPEN_WRITE)) return IOS27_24D_INVALID;
        if (!f->x[1]) return IOS27_24D_INVALID;
        char path[64]; if (!guest_str(p,f->x[0],path)) return IOS27_24D_FAULT;
        int fi=-1;
        for (unsigned i=0;i<IOS27_24D_FILES;i++)
            if (p->files[i].live && !strcmp(p->files[i].path,path)) {fi=(int)i;break;}
        if (fi<0) return IOS27_24D_DENIED;
        if ((f->x[1]&IOS27_24D_OPEN_WRITE) && !p->files[fi].writable) return IOS27_24D_DENIED;
        int fd=find_fd(p),of=find_ofd(p);
        if (fd<0 || of<0) return IOS27_24D_LIMIT;
        p->ofds[of]=(ofd_t){.live=1,.mode=(uint8_t)f->x[1],.refs=1,.file=(uint32_t)fi};
        p->fds[fd]=(fd_t){.live=1,.ofd=(uint16_t)of};
        *value=fd; return IOS27_24D_OK;
    }
    if (f->x[0]>=IOS27_24D_FDS) return IOS27_24D_INVALID;
    unsigned fd=(unsigned)f->x[0];
    if (!p->fds[fd].live) return IOS27_24D_INVALID;
    ofd_t *o=&p->ofds[p->fds[fd].ofd];
    if (n==IOS27_24D_SYS_CLOSE) {close_fd(p,fd);return IOS27_24D_OK;}
    if (n==IOS27_24D_SYS_DUP || n==IOS27_24D_SYS_DUP2) {
        int to;
        if (n==IOS27_24D_SYS_DUP) to=find_fd(p);
        else { if (f->x[1]>=IOS27_24D_FDS) return IOS27_24D_INVALID;to=(int)f->x[1]; }
        if (to<0) return IOS27_24D_LIMIT;
        if ((unsigned)to==fd) {*value=to;return IOS27_24D_OK;}
        if (p->fds[to].live) close_fd(p,(unsigned)to);
        if (o->refs==UINT16_MAX) return IOS27_24D_LIMIT;
        ++o->refs; p->fds[to]=(fd_t){.live=1,.ofd=p->fds[fd].ofd};
        *value=to;return IOS27_24D_OK;
    }
    if (n==IOS27_24D_SYS_FD_FLAGS) {
        if (f->x[1]>1 || f->x[2]>1) return IOS27_24D_INVALID;
        if (f->x[1]) p->fds[fd].cloexec=(uint8_t)f->x[2];
        *value=p->fds[fd].cloexec;return IOS27_24D_OK;
    }
    file_t *file=&p->files[o->file];
    if (n==IOS27_24D_SYS_LSEEK) {
        if (f->x[2]>2) return IOS27_24D_INVALID;
        int64_t pos=(f->x[2]==0)?0:(f->x[2]==1)?(int64_t)o->pos:(int64_t)file->size;
        int64_t offset=(int64_t)f->x[1];
        if ((offset>0 && pos>INT64_MAX-offset)||(offset<0 && pos<INT64_MIN-offset)) return IOS27_24D_INVALID;
        pos+=offset;
        if (pos<0 || pos>IOS27_24D_FILE_BYTES) return IOS27_24D_INVALID;
        o->pos=(size_t)pos;*value=pos;return IOS27_24D_OK;
    }
    if (n!=IOS27_24D_SYS_READ && n!=IOS27_24D_SYS_WRITE) return IOS27_24D_UNSUPPORTED;
    int wr=n==IOS27_24D_SYS_WRITE;
    if (!(o->mode & (wr?IOS27_24D_OPEN_WRITE:IOS27_24D_OPEN_READ))) return IOS27_24D_DENIED;
    if (f->x[2]>IOS27_24D_FILE_BYTES) return IOS27_24D_LIMIT;
    size_t len=(size_t)f->x[2];
    /* Validate entire requested guest buffer, even at EOF, before touching offsets. */
    if (!span(p,f->x[1],len,wr?IOS27_24D_READ:IOS27_24D_WRITE)) return IOS27_24D_FAULT;
    if (wr) {
        if (!file->writable) return IOS27_24D_DENIED;
        if (len>IOS27_24D_FILE_BYTES-o->pos) return IOS27_24D_LIMIT;
        memcpy(file->data+o->pos,p->memory+f->x[1],len);
        o->pos+=len;if (o->pos>file->size) file->size=o->pos;
    } else {
        if (o->pos>file->size) len=0;
        else if (len>file->size-o->pos) len=file->size-o->pos;
        memcpy(p->memory+f->x[1],file->data+o->pos,len);
        o->pos+=len;
    }
    *value=(int64_t)len;return IOS27_24D_OK;
}
static int lock_rt(ios27_24d_runtime *rt) { return rt && !pthread_mutex_lock(&rt->lock); }
int ios27_24d_new(ios27_24d_runtime **out) {
    if (!out) return IOS27_24D_INVALID;
    *out=NULL; ios27_24d_runtime *rt=calloc(1,sizeof(*rt));
    if (!rt) return IOS27_24D_LIMIT;
    if (pthread_mutex_init(&rt->lock,NULL)) {free(rt);return IOS27_24D_LIMIT;}
    if (pthread_cond_init(&rt->changed,NULL)) {pthread_mutex_destroy(&rt->lock);free(rt);return IOS27_24D_LIMIT;}
    rt->next_port=1;*out=rt;return IOS27_24D_OK;
}
void ios27_24d_free(ios27_24d_runtime *rt) {
    /* Caller must join all host worker threads before releasing runtime. */
    if (!rt) return;
    pthread_cond_destroy(&rt->changed);pthread_mutex_destroy(&rt->lock);free(rt);
}
int ios27_24d_process_create(ios27_24d_runtime *rt,uint64_t *pid) {
    if (!rt || !pid || !lock_rt(rt)) return IOS27_24D_INVALID;
    int rc=IOS27_24D_LIMIT;
    for (unsigned i=0;i<IOS27_24D_TASKS;i++) if (!rt->proc[i].live && rt->process_gen<UINT32_MAX) {
        proc_t *p=&rt->proc[i];memset(p,0,sizeof(*p));p->live=1;p->generation=++rt->process_gen;
        for (unsigned j=0;j<PAGES;j++) p->prot[j]=IOS27_24D_READ|IOS27_24D_WRITE;
        *pid=handle(i,p->generation);rt->stats.active_tasks++;rc=IOS27_24D_OK;break;
    }
    pthread_mutex_unlock(&rt->lock);return rc;
}
int ios27_24d_thread_create(ios27_24d_runtime *rt,uint64_t pid,uint64_t *tid) {
    if (!rt || !tid || !lock_rt(rt)) return IOS27_24D_INVALID;
    int task=find_proc(rt,pid),rc=IOS27_24D_LIMIT;
    if (task<0) rc=IOS27_24D_DEAD;
    else for (unsigned i=0;i<IOS27_24D_THREADS;i++) if (!rt->threads[i].live && rt->thread_gen<UINT32_MAX) {
        rt->threads[i]=(thread_t){.live=1,.owner=(uint16_t)task,.generation=++rt->thread_gen};
        *tid=handle(i,rt->thread_gen);rt->stats.active_threads++;rc=IOS27_24D_OK;break;
    }
    pthread_mutex_unlock(&rt->lock);return rc;
}
int ios27_24d_thread_cancel(ios27_24d_runtime *rt,uint64_t pid,uint64_t tid) {
    if (!lock_rt(rt)) return IOS27_24D_INVALID;
    int task=find_proc(rt,pid),thr=task<0?-1:find_thread(rt,task,tid);
    if (thr>=0) {rt->threads[thr].cancelled=1;pthread_cond_broadcast(&rt->changed);}
    pthread_mutex_unlock(&rt->lock);return thr>=0?IOS27_24D_OK:IOS27_24D_DEAD;
}
int ios27_24d_thread_destroy(ios27_24d_runtime *rt,uint64_t pid,uint64_t tid) {
    if (!lock_rt(rt)) return IOS27_24D_INVALID;
    int task=find_proc(rt,pid),thr=task<0?-1:find_thread(rt,task,tid);
    if (thr>=0) {rt->threads[thr].live=0;rt->stats.active_threads--;pthread_cond_broadcast(&rt->changed);}
    pthread_mutex_unlock(&rt->lock);return thr>=0?IOS27_24D_OK:IOS27_24D_DEAD;
}
int ios27_24d_memory_protect(ios27_24d_runtime *rt,uint64_t pid,uint32_t page,uint32_t perms) {
    if (!lock_rt(rt)) return IOS27_24D_INVALID;
    int task=find_proc(rt,pid),rc=task<0?IOS27_24D_DEAD:IOS27_24D_INVALID;
    if (task>=0 && page<PAGES && !(perms&~(IOS27_24D_READ|IOS27_24D_WRITE))) {
        rt->proc[task].prot[page]=(uint8_t)perms;rc=IOS27_24D_OK;
    }
    pthread_mutex_unlock(&rt->lock);return rc;
}
static int memory_access(ios27_24d_runtime *rt,uint64_t pid,uint64_t addr,void *bytes,size_t len,int write) {
    if (!lock_rt(rt)) return IOS27_24D_INVALID;
    int task=find_proc(rt,pid);
    int rc=task<0?IOS27_24D_DEAD:fault_or_copy(&rt->proc[task],addr,bytes,len,write);
    if (rc==IOS27_24D_OK) rt->stats.copies++;
    pthread_mutex_unlock(&rt->lock);return rc;
}
int ios27_24d_memory_write(ios27_24d_runtime *rt,uint64_t pid,uint64_t addr,const void *src,size_t len) {
    return memory_access(rt,pid,addr,(void *)src,len,1);
}
int ios27_24d_memory_read(ios27_24d_runtime *rt,uint64_t pid,uint64_t addr,void *dst,size_t len) {
    return memory_access(rt,pid,addr,dst,len,0);
}
int ios27_24d_seed_file(ios27_24d_runtime *rt,uint64_t pid,const char *path,const void *bytes,size_t len) {
    if (!rt || !path || !bytes || len>IOS27_24D_FILE_BYTES ||
        strncmp(path,"/fixture/",9) || strlen(path)>=64 || !lock_rt(rt)) return IOS27_24D_INVALID;
    int task=find_proc(rt,pid),rc=task<0?IOS27_24D_DEAD:IOS27_24D_LIMIT;
    if (task>=0) {
        proc_t *p=&rt->proc[task];
        for (unsigned i=0;i<IOS27_24D_FILES;i++) if (!p->files[i].live) {
            file_t *f=&p->files[i];f->live=1;f->writable=1;
            memcpy(f->path,path,strlen(path)+1);memcpy(f->data,bytes,len);f->size=len;
            rc=IOS27_24D_OK;break;
        }
    }
    pthread_mutex_unlock(&rt->lock);return rc;
}
static void destroy_port(ios27_24d_runtime *rt,unsigned i) {
    port_t *p=&rt->ports[i];
    for (unsigned t=0;t<IOS27_24D_TASKS;t++) if ((p->rights[t] || p->once[t]) && rt->proc[t].live) {
        proc_t *owner=&rt->proc[t];
        if (owner->dead_write-owner->dead_read<IOS27_24D_PORTS)
            owner->dead[owner->dead_write++ % IOS27_24D_PORTS]=p->name;
        rt->stats.dead_notifications++;
    }
    rt->stats.queued-=p->used;rt->stats.cleaned_ports++;
    memset(p,0,sizeof(*p));pthread_cond_broadcast(&rt->changed);
}
int ios27_24d_process_destroy(ios27_24d_runtime *rt,uint64_t pid) {
    if (!lock_rt(rt)) return IOS27_24D_INVALID;
    int task=find_proc(rt,pid);
    if (task<0) {pthread_mutex_unlock(&rt->lock);return IOS27_24D_DEAD;}
    proc_t *p=&rt->proc[task];
    /* First retire owned ports, then all task rights, threads and descriptors. */
    for (unsigned i=0;i<IOS27_24D_PORTS;i++)
        if (rt->ports[i].live && rt->ports[i].owner==(unsigned)task) destroy_port(rt,i);
    for (unsigned i=0;i<IOS27_24D_PORTS;i++) {rt->ports[i].rights[task]=0;rt->ports[i].once[task]=0;}
    for (unsigned i=0;i<IOS27_24D_THREADS;i++) if (rt->threads[i].live && rt->threads[i].owner==(unsigned)task) {
        rt->threads[i].live=0;rt->stats.active_threads--;
    }
    for (unsigned i=0;i<IOS27_24D_FDS;i++) if (p->fds[i].live) {
        close_fd(p,i);rt->stats.cleaned_fds++;
    }
    p->live=0;rt->stats.active_tasks--;
    pthread_cond_broadcast(&rt->changed);pthread_mutex_unlock(&rt->lock);return IOS27_24D_OK;
}
int ios27_24d_port_allocate(ios27_24d_runtime *rt,uint64_t pid,uint64_t *name) {
    if (!name || !lock_rt(rt)) return IOS27_24D_INVALID;
    int task=find_proc(rt,pid),rc=task<0?IOS27_24D_DEAD:IOS27_24D_LIMIT;
    if (task>=0 && rt->next_port<UINT64_MAX) for (unsigned i=0;i<IOS27_24D_PORTS;i++)
        if (!rt->ports[i].live) {
            port_t *p=&rt->ports[i];memset(p,0,sizeof(*p));p->live=1;p->owner=(uint16_t)task;
            p->name=rt->next_port++;p->rights[task]=1;*name=p->name;rc=IOS27_24D_OK;break;
        }
    pthread_mutex_unlock(&rt->lock);return rc;
}
int ios27_24d_port_grant(ios27_24d_runtime *rt,uint64_t owner,uint64_t name,uint64_t target,int once) {
    if ((once!=0 && once!=1) || !lock_rt(rt)) return IOS27_24D_INVALID;
    int from=find_proc(rt,owner),to=find_proc(rt,target),idx=find_port(rt,name);
    int rc=IOS27_24D_DEAD;
    if (from>=0 && to>=0 && idx>=0) {
        port_t *p=&rt->ports[idx];
        if (p->owner!=(unsigned)from) rc=IOS27_24D_DENIED;
        else if (once?p->once[to]==UINT8_MAX:p->rights[to]==UINT8_MAX) rc=IOS27_24D_LIMIT;
        else {if (once) p->once[to]++;else p->rights[to]++;rc=IOS27_24D_OK;}
    }
    pthread_mutex_unlock(&rt->lock);return rc;
}
int ios27_24d_port_destroy(ios27_24d_runtime *rt,uint64_t owner,uint64_t name) {
    if (!lock_rt(rt)) return IOS27_24D_INVALID;
    int from=find_proc(rt,owner),idx=find_port(rt,name),rc=IOS27_24D_DEAD;
    if (from>=0 && idx>=0) {
        if (rt->ports[idx].owner!=(unsigned)from) rc=IOS27_24D_DENIED;
        else {destroy_port(rt,(unsigned)idx);rc=IOS27_24D_OK;}
    }
    pthread_mutex_unlock(&rt->lock);return rc;
}
int ios27_24d_poll_dead(ios27_24d_runtime *rt,uint64_t pid,uint64_t *name) {
    if (!name || !lock_rt(rt)) return IOS27_24D_INVALID;
    int task=find_proc(rt,pid),rc=task<0?IOS27_24D_DEAD:IOS27_24D_TIMEOUT;
    if (task>=0) {
        proc_t *p=&rt->proc[task];
        if (p->dead_read<p->dead_write) {*name=p->dead[p->dead_read++ % IOS27_24D_PORTS];rc=IOS27_24D_OK;}
    }
    pthread_mutex_unlock(&rt->lock);return rc;
}
static int add_ms(struct timespec *when,uint32_t milliseconds) {
    if (clock_gettime(CLOCK_REALTIME,when)) return 0;
    when->tv_sec+=(time_t)(milliseconds/1000);
    when->tv_nsec+=(long)(milliseconds%1000)*1000000L;
    if (when->tv_nsec>=1000000000L) {when->tv_sec++;when->tv_nsec-=1000000000L;}
    return 1;
}
static int wait_port(ios27_24d_runtime *rt,unsigned idx,uint64_t expected_port,
                     uint64_t pid,uint64_t tid,int task,int thread,
                     int sending,uint32_t timeout_ms) {
    if (timeout_ms>2000) return IOS27_24D_INVALID;
    struct timespec deadline;
    if (timeout_ms && !add_ms(&deadline,timeout_ms)) return IOS27_24D_INVALID;
    for (;;) {
        if (find_proc(rt,pid)!=task || find_thread(rt,task,tid)!=thread) return IOS27_24D_DEAD;
        if (rt->threads[thread].cancelled) {rt->stats.cancelled++;return IOS27_24D_INTERRUPTED;}
        port_t *p=&rt->ports[idx];
        if (!p->live || p->name!=expected_port) return IOS27_24D_DEAD;
        if (sending?p->used<IOS27_24D_QUEUE:p->used>0) return IOS27_24D_OK;
        if (!timeout_ms) {rt->stats.timed_out++;return IOS27_24D_TIMEOUT;}
        rt->stats.waits++;
        int rc=pthread_cond_timedwait(&rt->changed,&rt->lock,&deadline);
        if (rc==ETIMEDOUT) {rt->stats.timed_out++;return IOS27_24D_TIMEOUT;}
        if (rc!=0) return IOS27_24D_INVALID;
    }
}
static int send_message(ios27_24d_runtime *rt,int task,int thr,uint64_t port_name,uint64_t addr,
                        uint64_t length,uint32_t timeout,uint64_t pid,uint64_t tid) {
    if (length!=sizeof(ios27_24d_message)) return IOS27_24D_BAD_ABI;
    proc_t *proc=&rt->proc[task];
    if (!span(proc,addr,length,IOS27_24D_READ)) return IOS27_24D_FAULT;
    ios27_24d_message msg;
    memcpy(&msg,proc->memory+addr,sizeof(msg));
    if (!msg.correlation || msg.size>IOS27_24D_PAYLOAD ||
        msg.opcode==0) return IOS27_24D_INVALID;
    int idx=find_port(rt,port_name);
    if (idx<0) return IOS27_24D_DEAD;
    port_t *p=&rt->ports[idx];
    if (!p->rights[task] && !p->once[task]) return IOS27_24D_DENIED;
    int reply=-1,transfer=-1;
    if (msg.reply_port && msg.reply_port==msg.transfer_port) return IOS27_24D_INVALID;
    if (msg.reply_port && msg.correlation<=rt->threads[thr].last_corr) return IOS27_24D_INVALID;
    if (msg.reply_port) {
        reply=find_port(rt,msg.reply_port);
        if (reply<0 || rt->ports[reply].owner!=(unsigned)task ||
            rt->ports[reply].expected_corr || rt->ports[reply].once[p->owner]==UINT8_MAX)
            return IOS27_24D_DENIED;
    }
    if (msg.transfer_port) {
        transfer=find_port(rt,msg.transfer_port);
        if (transfer<0 || (!rt->ports[transfer].rights[task] && !rt->ports[transfer].once[task]) ||
            rt->ports[transfer].rights[p->owner]==UINT8_MAX) return IOS27_24D_DENIED;
    }
    if (p->expected_corr && (msg.correlation!=p->expected_corr || msg.reply_port)) return IOS27_24D_DENIED;
    int rc=wait_port(rt,(unsigned)idx,port_name,pid,tid,task,thr,1,timeout);
    if (rc!=IOS27_24D_OK) return rc;
    /* Revalidate rights/recipients after dropping mutex inside timed wait. */
    if ((!p->rights[task]&&!p->once[task]) ||
        (reply>=0 && (!rt->ports[reply].live || rt->ports[reply].expected_corr)) ||
        (transfer>=0 && (!rt->ports[transfer].live ||
           (!rt->ports[transfer].rights[task]&&!rt->ports[transfer].once[task]))))
        return IOS27_24D_DEAD;
    if (p->once[task]) p->once[task]--;
    if (reply>=0) {rt->ports[reply].expected_corr=msg.correlation;rt->ports[reply].once[p->owner]++;rt->threads[thr].last_corr=msg.correlation;}
    if (transfer>=0) {
        port_t *t=&rt->ports[transfer];
        if (t->once[task]) t->once[task]--;else t->rights[task]--;
        t->rights[p->owner]++;
    }
    p->queue[(p->head+p->used)%IOS27_24D_QUEUE]=msg;
    p->used++;rt->stats.queued++;rt->stats.sends++;
    pthread_cond_broadcast(&rt->changed);return IOS27_24D_OK;
}
static int receive_message(ios27_24d_runtime *rt,int task,int thr,uint64_t name,uint64_t addr,
                           uint64_t capacity,uint32_t timeout,int64_t *value,uint64_t *corr,uint64_t pid,uint64_t tid) {
    if (capacity<sizeof(ios27_24d_message)) return IOS27_24D_LIMIT;
    proc_t *proc=&rt->proc[task];
    if (!span(proc,addr,sizeof(ios27_24d_message),IOS27_24D_WRITE)) return IOS27_24D_FAULT;
    int idx=find_port(rt,name);
    if (idx<0) return IOS27_24D_DEAD;
    port_t *p=&rt->ports[idx];
    if (p->owner!=(unsigned)task) return IOS27_24D_DENIED;
    int rc=wait_port(rt,(unsigned)idx,name,pid,tid,task,thr,0,timeout);
    if (rc!=IOS27_24D_OK) return rc;
    /* The buffer may have been protected by another call during the wait. */
    if (!span(proc,addr,sizeof(ios27_24d_message),IOS27_24D_WRITE)) return IOS27_24D_FAULT;
    ios27_24d_message msg=p->queue[p->head];
    memcpy(proc->memory+addr,&msg,sizeof(msg));
    p->head=(p->head+1)%IOS27_24D_QUEUE;p->used--;
    if (p->expected_corr) p->expected_corr=0;
    rt->stats.queued--;rt->stats.receives++;
    *value=(int64_t)sizeof(msg);*corr=msg.correlation;
    pthread_cond_broadcast(&rt->changed);return IOS27_24D_OK;
}
static unsigned argument_count(uint32_t syscall) {
    switch (syscall) {
        case IOS27_24D_SYS_GETPID: return 0;
        case IOS27_24D_SYS_CLOSE: case IOS27_24D_SYS_DUP: return 1;
        case IOS27_24D_SYS_OPEN: case IOS27_24D_SYS_DUP2: return 2;
        case IOS27_24D_SYS_READ: case IOS27_24D_SYS_WRITE:
        case IOS27_24D_SYS_LSEEK: case IOS27_24D_SYS_FD_FLAGS: return 3;
        case IOS27_24D_SYS_IPC_SEND: case IOS27_24D_SYS_IPC_RECV: return 4;
        default: return 6;
    }
}
int ios27_24d_trap(ios27_24d_runtime *rt,uint64_t pid,uint64_t tid,
                   const ios27_24d_frame *frame,ios27_24d_result *result) {
    if (!frame || !result || !lock_rt(rt)) return IOS27_24D_INVALID;
    ios27_24d_frame f=*frame;
    ios27_24d_result r={.status=IOS27_24D_UNSUPPORTED,.value=-1,.correlation=0};
    rt->stats.calls++;
    int task=find_proc(rt,pid),thr=task<0?-1:find_thread(rt,task,tid);
    if (task<0 || thr<0) r.status=IOS27_24D_DEAD;
    else if (f.abi!=IOS27_24D_ABI) r.status=IOS27_24D_BAD_ABI;
    else if (rt->threads[thr].cancelled) {r.status=IOS27_24D_INTERRUPTED;rt->stats.cancelled++;}
    else if (argument_count(f.number)<6 && (f.x[5] || f.x[4] ||
             (argument_count(f.number)<=3 && f.x[3]) ||
             (argument_count(f.number)<=2 && f.x[2]) ||
             (argument_count(f.number)<=1 && f.x[1]) ||
             (argument_count(f.number)==0 && f.x[0]))) r.status=IOS27_24D_BAD_ABI;
    else if (f.number==IOS27_24D_SYS_GETPID) {r.status=IOS27_24D_OK;r.value=(int64_t)pid;}
    else if (f.number==IOS27_24D_SYS_IPC_SEND || f.number==IOS27_24D_SYS_IPC_RECV) {
        if (f.x[3]>UINT32_MAX) r.status=IOS27_24D_INVALID;
        else if (f.number==IOS27_24D_SYS_IPC_SEND) {
            r.status=send_message(rt,task,thr,f.x[0],f.x[1],f.x[2],(uint32_t)f.x[3],pid,tid);
            if (!r.status) r.value=(int64_t)sizeof(ios27_24d_message);
        } else r.status=receive_message(rt,task,thr,f.x[0],f.x[1],f.x[2],(uint32_t)f.x[3],&r.value,&r.correlation,pid,tid);
    } else if (f.number==IOS27_24D_SYS_OPEN || f.number==IOS27_24D_SYS_READ ||
               f.number==IOS27_24D_SYS_WRITE || f.number==IOS27_24D_SYS_CLOSE ||
               f.number==IOS27_24D_SYS_DUP || f.number==IOS27_24D_SYS_DUP2 ||
               f.number==IOS27_24D_SYS_LSEEK || f.number==IOS27_24D_SYS_FD_FLAGS) {
        r.status=fd_dispatch(rt,&rt->proc[task],&f,&r.value);
    }
    if (r.status!=IOS27_24D_OK) rt->stats.rejected++;
    *result=r;pthread_mutex_unlock(&rt->lock);return r.status;
}
void ios27_24d_get_stats(ios27_24d_runtime *rt,ios27_24d_stats *out) {
    if (!out || !lock_rt(rt)) return;
    *out=rt->stats;pthread_mutex_unlock(&rt->lock);
}
