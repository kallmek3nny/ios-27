#include "ios27/libsystem20e_services.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

int phase20e_compiled_client(ios27_20e_ctx *c);
static ios27_20e_ctx c;
static void gueststr(uint64_t p,const char *s){size_t n=strlen(s)+1; assert(p+n<32768);memcpy(c.guest+p,s,n);}
static void basics(void){
    int64_t fd;size_t i;
    ios27_20e_init(&c);
    assert(c.guest_pid==101 && ios27_20e_syscall(&c,20,0,0,0,0,0,0)==101);
    assert(ios27_20e_syscall(&c,0xffff,0,0,0,0,0,0)==-1 && c.guest_errno==IOS27_20E_ENOSYS);
    assert(ios27_20e_seed_file(&c,"/app/config",(const uint8_t*)"hello",5,0)==0);
    gueststr(256,"/app/config");
    fd=ios27_20e_syscall(&c,5,256,IOS27_20E_RDONLY,0,0,0,0);
    assert(fd>=3 && ios27_20e_read(&c,(int)fd,512,5)==5);
    assert(!memcmp(c.guest+512,"hello",5));
    assert(ios27_20e_lseek(&c,(int)fd,0,IOS27_20E_SEEK_SET)==0);
    assert(ios27_20e_read(&c,(int)fd,512,7)==5);
    assert(ios27_20e_read(&c,(int)fd,512,1)==0);
    assert(ios27_20e_write(&c,(int)fd,512,1)==-1 && c.guest_errno==IOS27_20E_EBADF);
    assert(ios27_20e_close(&c,(int)fd)==0 && ios27_20e_close(&c,(int)fd)==-1);
    assert(c.guest_errno==IOS27_20E_EBADF);
    assert(ios27_20e_open(&c,256,IOS27_20E_WRONLY)==-1 && c.guest_errno==IOS27_20E_EROFS);
    gueststr(256,"/../../etc/passwd");
    assert(ios27_20e_open(&c,256,IOS27_20E_RDONLY)==-1 && c.guest_errno==IOS27_20E_EACCES);
    gueststr(256,"/tmp/append");
    fd=ios27_20e_open(&c,256,IOS27_20E_CREAT|IOS27_20E_RDWR);
    assert(fd>=3);
    memcpy(c.guest+768,"test",4);
    assert(ios27_20e_write(&c,(int)fd,768,4)==4);
    assert(ios27_20e_lseek(&c,(int)fd,-2,IOS27_20E_SEEK_END)==2);
    assert(ios27_20e_write(&c,(int)fd,768,1)==1);
    assert(ios27_20e_lseek(&c,(int)fd,0,IOS27_20E_SEEK_SET)==0);
    assert(ios27_20e_read(&c,(int)fd,900,4)==4);
    assert(!memcmp(c.guest+900,"tett",4));
    assert(ios27_20e_close(&c,(int)fd)==0);
    for(i=0;i<IOS27_20E_FDS-3;++i){fd=ios27_20e_open(&c,256,IOS27_20E_RDONLY);assert(fd==(int64_t)(i+3));}
    assert(ios27_20e_open(&c,256,IOS27_20E_RDONLY)==-1 && c.guest_errno==IOS27_20E_EMFILE);
    for(i=3;i<IOS27_20E_FDS;++i)assert(ios27_20e_close(&c,(int)i)==0);
    assert(ios27_20e_write(&c,1,768,4)==4 && c.output_len==4 && !memcmp(c.output,"test",4));
    assert(ios27_20e_write(&c,1,IOS27_20E_BYTES-2,4)==-1 && c.guest_errno==IOS27_20E_EFAULT);
    assert(c.output_len==4);
    assert(ios27_20e_syscall(&c,4,UINT64_C(0x100000001),768,4,0,0,0)==-1 && c.guest_errno==IOS27_20E_EBADF);
    assert(ios27_20e_syscall(&c,5,256,UINT64_C(0x100000000),0,0,0,0)==-1 && c.guest_errno==IOS27_20E_EINVAL);
    assert(ios27_20e_open(&c,UINT64_MAX-2,IOS27_20E_RDONLY)==-1 && c.guest_errno==IOS27_20E_EFAULT);
}
static void environment_time(void){
    int64_t secs, micros;
    ios27_20e_init(&c);
    assert(ios27_20e_setenv(&c,"USER","mobile")==0);
    gueststr(256,"USER");
    assert(ios27_20e_getenv(&c,256,512,4)==-1 && c.guest_errno==IOS27_20E_ERANGE);
    assert(ios27_20e_getenv(&c,256,512,20)==7 && !strcmp((char*)c.guest+512,"mobile"));
    gueststr(256,"MISSING");assert(ios27_20e_getenv(&c,256,512,20)==0);
    assert(ios27_20e_gettimeofday(&c,1024)==0);
    memcpy(&secs,c.guest+1024,8);memcpy(&micros,c.guest+1032,8);
    assert(secs==1700000000 && micros==0);
    assert(ios27_20e_advance_time(&c,2300001)==0);
    assert(ios27_20e_gettimeofday(&c,1024)==0);
    memcpy(&secs,c.guest+1024,8);memcpy(&micros,c.guest+1032,8);
    assert(secs==1700000002 && micros==300001);
    assert(ios27_20e_gettimeofday(&c,IOS27_20E_BYTES-8)==-1 && c.guest_errno==IOS27_20E_EFAULT);
    assert(ios27_20e_advance_time(&c,UINT64_MAX)==0);
    assert(ios27_20e_advance_time(&c,UINT64_MAX)==0); /* no host clock dependency */
}
static void allocations(void){
    uint64_t arr[IOS27_20E_BLOCKS],i,j,old,addr;
    ios27_20e_init(&c);
    assert(ios27_20e_calloc(&c,UINT64_MAX,8)==0 && c.guest_errno==IOS27_20E_ENOMEM);
    for(i=0;i<IOS27_20E_BLOCKS;++i){
        arr[i]=ios27_20e_malloc(&c,13+i);
        assert(arr[i]!=0 && arr[i]%16==0);
        for(j=0;j<i;++j)assert(arr[i]!=arr[j]);
    }
    assert(ios27_20e_malloc(&c,8)==0 && c.guest_errno==IOS27_20E_ENOMEM);
    for(i=0;i<IOS27_20E_BLOCKS;i+=2)assert(ios27_20e_free(&c,arr[i])==0);
    old=arr[1];c.guest[old]=0xAB;
    addr=ios27_20e_realloc(&c,old,200);
    assert(addr && c.guest[addr]==0xAB);
    assert(ios27_20e_free(&c,old)==-1 && c.guest_errno==IOS27_20E_EINVAL);
    assert(ios27_20e_free(&c,addr)==0);
    for(i=3;i<IOS27_20E_BLOCKS;i+=2)assert(ios27_20e_free(&c,arr[i])==0);
    assert(ios27_20e_malloc(&c,32768)==0 && c.guest_errno==IOS27_20E_ENOMEM);
    addr=ios27_20e_calloc(&c,100,10);assert(addr && c.guest[addr+999]==0);
    assert(ios27_20e_free(&c,addr)==0);
    for(i=0;i<5000;++i){addr=ios27_20e_malloc(&c,(i%100)+1);assert(addr);assert(ios27_20e_free(&c,addr)==0);}
}
static void mappings(void){
    int64_t a;uint64_t i;
    ios27_20e_init(&c);
    assert(ios27_20e_mmap(&c,0,4096,7,IOS27_20E_MAP_PRIVATE|IOS27_20E_MAP_ANON,-1,0)==-1);
    assert(ios27_20e_mmap(&c,0,4096,3,IOS27_20E_MAP_PRIVATE|IOS27_20E_MAP_ANON,0,0)==-1);
    a=ios27_20e_mmap(&c,0,6000,3,IOS27_20E_MAP_PRIVATE|IOS27_20E_MAP_ANON,-1,0);
    assert(a==32768 && ios27_20e_guest_ptr(&c,(uint64_t)a,8192,1));
    c.guest[a+4096]=0x77;
    assert(ios27_20e_mprotect(&c,(uint64_t)a+4096,4096,1)==0);
    assert(!ios27_20e_guest_ptr(&c,(uint64_t)a,8192,1));
    assert(ios27_20e_guest_ptr(&c,(uint64_t)a,8192,0));
    assert(ios27_20e_munmap(&c,(uint64_t)a,8192)==0);
    assert(!ios27_20e_guest_ptr(&c,(uint64_t)a,1,0));
    assert(ios27_20e_munmap(&c,(uint64_t)a,8192)==-1);
    for(i=0;i<IOS27_20E_MAP_PAGES;++i)assert(ios27_20e_mmap(&c,0,4096,3,0x1002,-1,0)>=0);
    assert(ios27_20e_mmap(&c,0,4096,3,0x1002,-1,0)==-1 && c.guest_errno==IOS27_20E_ENOMEM);
    assert(ios27_20e_mprotect(&c,IOS27_20E_BYTES,4096,1)==-1);
    assert(ios27_20e_syscall(&c,197,0,4096,3,0x1002,UINT64_MAX,0)==-1 && c.guest_errno==IOS27_20E_ENOMEM);
    assert(ios27_20e_syscall(&c,116,1024,1,0,0,0,0)==-1 && c.guest_errno==IOS27_20E_ENOSYS);
}
static void compiled(void){ios27_20e_init(&c);assert(phase20e_compiled_client(&c)==0);}
int main(void){basics();environment_time();allocations();mappings();compiled();puts("phase20e: native ABI fixture + sandbox services pass");return 0;}
