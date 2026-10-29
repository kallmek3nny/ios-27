#include "ios27/libsystem20e_services.h"
#include <limits.h>
#include <string.h>

static int64_t err(ios27_20e_ctx *c, int n) { c->guest_errno=n; return -1; }
static uint64_t a16(uint64_t n) { return (n+15u)&~UINT64_C(15); }
static size_t bounded_len(const char *s, size_t limit) {
    size_t i; if (!s) return limit;
    for(i=0;i<limit && s[i];++i) {} return i;
}
static int legal_path(const char *p) {
    size_t i, n=bounded_len(p,96);
    if(n<2 || n==96 || p[0]!='/') return 0;
    if (p[1]!='t' && p[1]!='a') return 0;
    if(strncmp(p,"/tmp/",5)!=0 && strncmp(p,"/app/",5)!=0) return 0;
    for(i=1;i<n;++i) if(p[i]=='\\' || (p[i]=='.' && (p[i+1]=='.' || (i && p[i-1]=='.')))) return 0;
    return p[n-1]!='/';
}
static int file_find(ios27_20e_ctx *c,const char *path){
    int i; for(i=0;i<(int)IOS27_20E_FILES;++i) if(c->files[i].used && strcmp(c->files[i].path,path)==0) return i;
    return -1;
}
void ios27_20e_init(ios27_20e_ctx *c){
    if(!c)return;
    memset(c,0,sizeof(*c)); c->guest_pid=101; c->guest_uid=501; c->guest_gid=20;
    c->unix_seconds=1700000000;
    c->fds[0].used=c->fds[1].used=c->fds[2].used=1;
    c->fds[0].kind=1; c->fds[1].kind=2; c->fds[2].kind=3;
}
int ios27_20e_seed_file(ios27_20e_ctx *c,const char *p,const uint8_t *d,size_t n,int writable){
    int i;
    if(!c || !legal_path(p) || n>IOS27_20E_FILE_BYTES || (n && !d))return -1;
    if(file_find(c,p)>=0)return -1;
    for(i=0;i<(int)IOS27_20E_FILES;++i)if(!c->files[i].used){
        ios27_20e_file *f=&c->files[i]; f->used=1; f->writable=(uint8_t)!!writable;
        memcpy(f->path,p,strlen(p)+1); if(n)memcpy(f->data,d,n);f->len=n;return 0;
    }
    return -1;
}
int ios27_20e_setenv(ios27_20e_ctx *c,const char *k,const char *v){
    size_t kn=bounded_len(k,32),vn=bounded_len(v,128); uint8_t i;
    if(!c || !kn || kn>=32 || vn>=128)return -1;
    for(i=0;i<kn;++i) if(k[i]=='=')return -1;
    for(i=0;i<c->env_count;++i)if(!strcmp(c->env_key[i],k))break;
    if(i==8)return -1;
    if(i==c->env_count){if(c->env_count==8)return -1;++c->env_count;}
    memcpy(c->env_key[i],k,kn+1);memcpy(c->env_val[i],v,vn+1);return 0;
}
void *ios27_20e_guest_ptr(ios27_20e_ctx *c,uint64_t addr,uint64_t n,int write_access){
    uint64_t end,pg;
    if(!c || !addr || addr>=IOS27_20E_BYTES || n>IOS27_20E_BYTES-addr)return NULL;
    if(n==0)return c->guest+addr;
    end=addr+n-1;
    for(pg=addr/IOS27_20E_PAGE;pg<=end/IOS27_20E_PAGE;++pg){
        if(pg>=IOS27_20E_MAP_FIRST){
            ios27_20e_page *p=&c->pages[pg-IOS27_20E_MAP_FIRST];
            if(!p->mapped || !(p->prot & (write_access?IOS27_20E_PROT_WRITE:IOS27_20E_PROT_READ)))return NULL;
        }
    }
    return c->guest+addr;
}
static int guest_string(ios27_20e_ctx *c,uint64_t addr,char *out,size_t size){
    size_t i; if(!size)return 0;
    for(i=0;i<size;++i){
        if(addr>UINT64_MAX-i)return 0;
        const char *p=(const char*)ios27_20e_guest_ptr(c,addr+i,1,0);
        if(!p)return 0;
        out[i]=*p;
        if(!*p)return 1;
    }return 0;
}
int64_t ios27_20e_getenv(ios27_20e_ctx *c,uint64_t name,uint64_t dst,uint64_t cap){
    char key[32];uint8_t i;size_t n;
    if(!c)return -1;
    if(!guest_string(c,name,key,sizeof(key)))return err(c,IOS27_20E_EFAULT);
    for(i=0;i<c->env_count;++i)if(!strcmp(c->env_key[i],key))break;
    if(i==c->env_count)return 0;
    n=strlen(c->env_val[i])+1;
    if(cap<n)return err(c,IOS27_20E_ERANGE);
    if(!ios27_20e_guest_ptr(c,dst,n,1))return err(c,IOS27_20E_EFAULT);
    memcpy(c->guest+dst,c->env_val[i],n); return (int64_t)n;
}
int64_t ios27_20e_open(ios27_20e_ctx *c,uint64_t gp,int flags){
    char p[96];int i,fi=-1,slot=-1,mode=flags&3;
    if(!c)return -1;
    if(flags&~(3|IOS27_20E_APPEND|IOS27_20E_CREAT|IOS27_20E_TRUNC))return err(c,IOS27_20E_EINVAL);
    if(mode==3 || ((flags&IOS27_20E_TRUNC) && mode==IOS27_20E_RDONLY))return err(c,IOS27_20E_EINVAL);
    if(!guest_string(c,gp,p,sizeof(p)))return err(c,IOS27_20E_EFAULT);
    if(!legal_path(p))return err(c,IOS27_20E_EACCES);
    for(i=3;i<(int)IOS27_20E_FDS;++i)if(!c->fds[i].used){slot=i;break;}
    if(slot<0)return err(c,IOS27_20E_EMFILE);
    fi=file_find(c,p);
    if(fi<0){
        if(!(flags&IOS27_20E_CREAT))return err(c,IOS27_20E_ENOENT);
        if(strncmp(p,"/tmp/",5))return err(c,IOS27_20E_EROFS);
        for(i=0;i<(int)IOS27_20E_FILES;++i)if(!c->files[i].used){fi=i;break;}
        if(fi<0)return err(c,IOS27_20E_ENOSPC);
        c->files[fi].used=1;c->files[fi].writable=1;
        memcpy(c->files[fi].path,p,strlen(p)+1);
    }
    if(mode!=IOS27_20E_RDONLY && !c->files[fi].writable)return err(c,IOS27_20E_EROFS);
    if(flags&IOS27_20E_TRUNC){memset(c->files[fi].data,0,IOS27_20E_FILE_BYTES);c->files[fi].len=0;}
    c->fds[slot].used=1;c->fds[slot].kind=4;c->fds[slot].mode=(uint8_t)flags;
    c->fds[slot].pos=(flags&IOS27_20E_APPEND)?c->files[fi].len:0;
    c->fds[slot].pad=(uint8_t)fi;
    return slot;
}
int64_t ios27_20e_close(ios27_20e_ctx *c,int fd){
    if(!c)return -1;
    if(fd<3 || fd>=(int)IOS27_20E_FDS || !c->fds[fd].used)return err(c,IOS27_20E_EBADF);
    memset(&c->fds[fd],0,sizeof(c->fds[fd]));return 0;
}
int64_t ios27_20e_read(ios27_20e_ctx *c,int fd,uint64_t dst,uint64_t n){
    ios27_20e_fd *d;const uint8_t *src;size_t len,amt;
    if(!c)return -1;
    if(fd<0 || fd>=(int)IOS27_20E_FDS || !c->fds[fd].used)return err(c,IOS27_20E_EBADF);
    d=&c->fds[fd];if(d->kind==1){src=c->input;len=c->input_len;}
    else if(d->kind==4 && (d->mode&3)!=IOS27_20E_WRONLY){
        ios27_20e_file *f=&c->files[d->pad];src=f->data;len=f->len;
    }else return err(c,IOS27_20E_EBADF);
    if(n>INT64_MAX)return err(c,IOS27_20E_EINVAL);
    if(n && !ios27_20e_guest_ptr(c,dst,n,1))return err(c,IOS27_20E_EFAULT);
    amt=d->pos>=len?0:len-d->pos;if(n<amt)amt=(size_t)n;
    if(amt)memcpy(c->guest+dst,src+d->pos,amt);
    d->pos+=amt;return (int64_t)amt;
}
int64_t ios27_20e_write(ios27_20e_ctx *c,int fd,uint64_t src,uint64_t n){
    ios27_20e_fd *d;uint8_t *dest;size_t *len,pos;
    if(!c)return -1;
    if(fd<0 || fd>=(int)IOS27_20E_FDS || !c->fds[fd].used)return err(c,IOS27_20E_EBADF);
    d=&c->fds[fd];
    if(d->kind==2){dest=c->output;len=&c->output_len;pos=*len;}
    else if(d->kind==3){dest=c->errors;len=&c->errors_len;pos=*len;}
    else if(d->kind==4 && (d->mode&3)!=IOS27_20E_RDONLY){
        ios27_20e_file *f=&c->files[d->pad];dest=f->data;len=&f->len;
        pos=(d->mode&IOS27_20E_APPEND)?*len:d->pos;
    }else return err(c,IOS27_20E_EBADF);
    if(n>IOS27_20E_FILE_BYTES || pos>IOS27_20E_FILE_BYTES-n)return err(c,IOS27_20E_ENOSPC);
    if(n && !ios27_20e_guest_ptr(c,src,n,0))return err(c,IOS27_20E_EFAULT);
    if(d->kind==4 && pos>*len)memset(dest+*len,0,pos-*len);
    if(n)memcpy(dest+pos,c->guest+src,(size_t)n);
    if(pos+n>*len)*len=pos+(size_t)n;
    if(d->kind==4)d->pos=pos+(size_t)n;
    return (int64_t)n;
}
int64_t ios27_20e_lseek(ios27_20e_ctx *c,int fd,int64_t delta,int whence){
    ios27_20e_fd *d;int64_t base,where;
    if(!c)return -1;
    if(fd<3 || fd>=(int)IOS27_20E_FDS || !c->fds[fd].used || c->fds[fd].kind!=4)return err(c,IOS27_20E_EBADF);
    d=&c->fds[fd];
    if(whence==IOS27_20E_SEEK_SET)base=0;
    else if(whence==IOS27_20E_SEEK_CUR)base=(int64_t)d->pos;
    else if(whence==IOS27_20E_SEEK_END)base=(int64_t)c->files[d->pad].len;
    else return err(c,IOS27_20E_EINVAL);
    if(delta< -base || delta>INT64_MAX-base)return err(c,IOS27_20E_EINVAL);
    where=base+delta;if(where>IOS27_20E_FILE_BYTES)return err(c,IOS27_20E_EINVAL);
    d->pos=(size_t)where;return where;
}
int64_t ios27_20e_gettimeofday(ios27_20e_ctx *c,uint64_t dst){
    if(!c)return -1;
    if(!ios27_20e_guest_ptr(c,dst,16,1))return err(c,IOS27_20E_EFAULT);
    { int64_t us=c->microseconds;memcpy(c->guest+dst,&c->unix_seconds,8);memcpy(c->guest+dst+8,&us,8); }
    return 0;
}
int64_t ios27_20e_advance_time(ios27_20e_ctx *c,uint64_t us){
    uint64_t secs,rem,add;
    if(!c)return -1;
    secs=us/1000000u;rem=us%1000000u;
    add=((uint64_t)c->microseconds+rem)/1000000u;
    if(secs>(uint64_t)INT64_MAX-(uint64_t)c->unix_seconds-add)return err(c,IOS27_20E_EOVERFLOW);
    c->unix_seconds+=(int64_t)(secs+add);
    c->microseconds=(int32_t)(((uint64_t)c->microseconds+rem)%1000000u);return 0;
}
uint64_t ios27_20e_malloc(ios27_20e_ctx *c,uint64_t bytes){
    uint64_t start=256,end=IOS27_20E_MAP_FIRST*IOS27_20E_PAGE;int i,j,slot=-1;
    if(!c)return 0;
    if(!bytes)bytes=1;
    if(bytes>end-start || bytes>UINT64_MAX-15){err(c,IOS27_20E_ENOMEM);return 0;}
    bytes=a16(bytes);
    for(i=0;i<(int)IOS27_20E_BLOCKS;++i)if(!c->blocks[i].used){slot=i;break;}
    if(slot<0){err(c,IOS27_20E_ENOMEM);return 0;}
    while(start<=end-bytes){
        uint64_t next=start;int overlap=0;
        for(j=0;j<(int)IOS27_20E_BLOCKS;++j)if(c->blocks[j].used){
            uint64_t lo=c->blocks[j].addr,hi=lo+c->blocks[j].bytes;
            if(start<hi && lo<start+bytes){if(hi>next)next=hi;overlap=1;}
        }
        if(!overlap){c->blocks[slot].used=1;c->blocks[slot].addr=start;c->blocks[slot].bytes=bytes;
            memset(c->guest+start,0,(size_t)bytes);return start;}
        start=a16(next);
    }
    err(c,IOS27_20E_ENOMEM);return 0;
}
uint64_t ios27_20e_calloc(ios27_20e_ctx *c,uint64_t count,uint64_t bytes){
    if(count && bytes>UINT64_MAX/count){if(c)err(c,IOS27_20E_ENOMEM);return 0;}
    return ios27_20e_malloc(c,count*bytes);
}
int64_t ios27_20e_free(ios27_20e_ctx *c,uint64_t addr){
    int i;if(!c)return -1;if(!addr)return 0;
    for(i=0;i<(int)IOS27_20E_BLOCKS;++i)if(c->blocks[i].used && c->blocks[i].addr==addr){
        memset(c->guest+addr,0,(size_t)c->blocks[i].bytes);
        memset(&c->blocks[i],0,sizeof(c->blocks[i]));return 0;
    }
    return err(c,IOS27_20E_EINVAL);
}
uint64_t ios27_20e_realloc(ios27_20e_ctx *c,uint64_t addr,uint64_t bytes){
    int i;uint64_t replacement,old;
    if(!c)return 0;
    if(!addr)return ios27_20e_malloc(c,bytes);
    if(!bytes){ios27_20e_free(c,addr);return 0;}
    for(i=0;i<(int)IOS27_20E_BLOCKS;++i)if(c->blocks[i].used && c->blocks[i].addr==addr)break;
    if(i==(int)IOS27_20E_BLOCKS){err(c,IOS27_20E_EINVAL);return 0;}
    old=c->blocks[i].bytes;if(bytes<=old)return addr;
    replacement=ios27_20e_malloc(c,bytes);
    if(!replacement)return 0;
    memcpy(c->guest+replacement,c->guest+addr,(size_t)old);
    ios27_20e_free(c,addr);return replacement;
}
static int page_range(uint64_t addr,uint64_t len,uint64_t *first,uint64_t *count){
    uint64_t floor=(uint64_t)IOS27_20E_MAP_FIRST*IOS27_20E_PAGE;
    if(!len || addr<floor || addr%IOS27_20E_PAGE || len%IOS27_20E_PAGE || addr>IOS27_20E_BYTES || len>IOS27_20E_BYTES-addr)return 0;
    *first=addr/IOS27_20E_PAGE-IOS27_20E_MAP_FIRST;
    *count=len/IOS27_20E_PAGE;return 1;
}
int64_t ios27_20e_mmap(ios27_20e_ctx *c,uint64_t hint,uint64_t n,int prot,int flags,int fd,uint64_t off){
    uint64_t p,k,need;
    if(!c)return -1;
    if(hint || !n || n>IOS27_20E_MAP_PAGES*IOS27_20E_PAGE || n>UINT64_MAX-(IOS27_20E_PAGE-1u))return err(c,IOS27_20E_EINVAL);
    if(prot&~3 || flags!=(IOS27_20E_MAP_PRIVATE|IOS27_20E_MAP_ANON) || fd!=-1 || off)return err(c,IOS27_20E_EACCES);
    need=(n+IOS27_20E_PAGE-1u)/IOS27_20E_PAGE;
    for(p=0;p+need<=IOS27_20E_MAP_PAGES;++p){
        for(k=0;k<need && !c->pages[p+k].mapped;++k){}
        if(k==need){
            for(k=0;k<need;++k){c->pages[p+k].mapped=1;c->pages[p+k].prot=(uint8_t)prot;}
            memset(c->guest+(IOS27_20E_MAP_FIRST+p)*IOS27_20E_PAGE,0,(size_t)(need*IOS27_20E_PAGE));
            return (int64_t)((IOS27_20E_MAP_FIRST+p)*IOS27_20E_PAGE);
        }
    }return err(c,IOS27_20E_ENOMEM);
}
int64_t ios27_20e_mprotect(ios27_20e_ctx *c,uint64_t a,uint64_t n,int prot){
    uint64_t first,count,k;
    if(!c)return -1;
    if(prot&~3 || !page_range(a,n,&first,&count))return err(c,IOS27_20E_EINVAL);
    for(k=0;k<count;++k)if(!c->pages[first+k].mapped)return err(c,IOS27_20E_ENOMEM);
    for(k=0;k<count;++k)c->pages[first+k].prot=(uint8_t)prot;
    return 0;
}
int64_t ios27_20e_munmap(ios27_20e_ctx *c,uint64_t a,uint64_t n){
    uint64_t first,count,k;
    if(!c)return -1;
    if(!page_range(a,n,&first,&count))return err(c,IOS27_20E_EINVAL);
    for(k=0;k<count;++k)if(!c->pages[first+k].mapped)return err(c,IOS27_20E_ENOMEM);
    for(k=0;k<count;++k)memset(&c->pages[first+k],0,sizeof(c->pages[first+k]));
    memset(c->guest+a,0,(size_t)n);return 0;
}
int64_t ios27_20e_syscall(ios27_20e_ctx *c,uint32_t num,uint64_t x,uint64_t y,uint64_t z,uint64_t d,uint64_t e,uint64_t f){
    if(!c)return -1;
    switch(num){
        case 3:if(x>INT_MAX)return err(c,IOS27_20E_EBADF);return ios27_20e_read(c,(int)x,y,z);
        case 4:if(x>INT_MAX)return err(c,IOS27_20E_EBADF);return ios27_20e_write(c,(int)x,y,z);
        case 5:if(y>INT_MAX)return err(c,IOS27_20E_EINVAL);return ios27_20e_open(c,x,(int)y);
        case 6:if(x>INT_MAX)return err(c,IOS27_20E_EBADF);return ios27_20e_close(c,(int)x);
        case 20:return (int64_t)c->guest_pid;
        case 24:case 25:return (int64_t)c->guest_uid;
        case 43:case 47:return (int64_t)c->guest_gid;
        case 73:return ios27_20e_munmap(c,x,y);
        case 74:if(z>INT_MAX)return err(c,IOS27_20E_EINVAL);return ios27_20e_mprotect(c,x,y,(int)z);
        case 116:if(y)return err(c,IOS27_20E_ENOSYS);return ios27_20e_gettimeofday(c,x);
        case 197:if(z>INT_MAX || d>INT_MAX || (e>INT_MAX && e!=UINT64_MAX))return err(c,IOS27_20E_EINVAL);
            return ios27_20e_mmap(c,x,y,(int)z,(int)d,e==UINT64_MAX?-1:(int)e,f);
        case 199:{int64_t delta; if(x>INT_MAX || z>INT_MAX)return err(c,IOS27_20E_EINVAL);
            memcpy(&delta,&y,sizeof(delta));return ios27_20e_lseek(c,(int)x,delta,(int)z);}
        default:return err(c,IOS27_20E_ENOSYS);
    }
}
