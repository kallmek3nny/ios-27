#include "ios27/phase22b_syscalls.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SYS(c,n,a) ios27_22b_dispatch((c),IOS27_22B_BSD|(n),(a))
typedef struct { ios27_22b_context *ctx; ios27_20e_ctx *mem; int fd; unsigned id; } worker;
static ios27_22b_result call(ios27_22b_context *c,uint32_t n,
                         uint64_t a,uint64_t b,uint64_t d) {
    uint64_t args[6]={a,b,d,0,0,0};return SYS(c,n,args);
}
static ios27_22b_result private_call(ios27_22b_context *c,uint32_t n,
                         uint64_t a,uint64_t b) {
    uint64_t args[6]={a,b,0,0,0,0};
    return ios27_22b_dispatch(c,IOS27_22B_PRIVATE|n,args);
}
static int64_t succeed(ios27_22b_result r) {
    assert(r.carry==0 && r.darwin_errno==0);return r.x0;
}
static void fails(ios27_22b_result r,int err) {
    if(!(r.carry==1 && r.darwin_errno==err && r.x0==err)) fprintf(stderr,"failed errno: wanted %d got (%lld,%u,%d)\n",err,(long long)r.x0,(unsigned)r.carry,r.darwin_errno);
    assert(r.carry==1 && r.darwin_errno==err && r.x0==err);
}
static void *thread_run(void *v) {
    worker *w=(worker*)v;
    uint64_t addr=1024u+(uint64_t)w->id*128u;
    for(unsigned i=0;i<18000u;++i) {
        assert(succeed(call(w->ctx,IOS27_22B_LSEEK,(uint64_t)w->fd,0,0))==0);
        assert(succeed(call(w->ctx,IOS27_22B_READ,(uint64_t)w->fd,addr,16))==16);
        assert(memcmp(w->mem->guest+addr,"abcdefghijklmnop",16)==0);
        assert(succeed(call(w->ctx,IOS27_22B_GETPID,0,0,0))==1234);
    }
    return NULL;
}
static void base_test(void) {
    ios27_20e_ctx *mem=(ios27_20e_ctx*)calloc(1,sizeof(*mem));
    ios27_22b_context *c=NULL;
    uint64_t a[6]={0};
    ios27_22b_metadata meta={0};
    assert(mem);ios27_20e_init(mem);
    assert(ios27_22b_create(&c,mem,1234,501,20)==0);
    assert(ios27_22b_preopen(c,"alpha","abcdefghijklmnop",16,1)==0);
    assert(ios27_22b_preopen(c,"readonly","x",1,0)==0);
    assert(ios27_22b_preopen(c,"..","x",1,1)==-1);
    assert(ios27_22b_stdin(c,"abc",3)==0);
    assert(succeed(call(c,IOS27_22B_READ,0,128,3))==3);
    assert(memcmp(mem->guest+128,"abc",3)==0);
    assert(succeed(call(c,IOS27_22B_WRITE,1,128,3))==3);
    { char captured[4]={0};assert(ios27_22b_capture(c,1,captured,4)==3);
      assert(strcmp(captured,"abc")==0); }
    strcpy((char*)mem->guest+256,"/cap/alpha");
    int fd=(int)succeed(call(c,IOS27_22B_OPEN,256,IOS27_22B_O_RDWR,0));
    assert(fd==3);
    assert(succeed(call(c,IOS27_22B_READ,(uint64_t)fd,512,16))==16);
    assert(memcmp(mem->guest+512,"abcdefghijklmnop",16)==0);
    assert(ios27_22b_metadata_fd(c,fd,&meta)==0);
    assert(meta.position==16 && meta.size==16 && meta.mode==3);
    assert(succeed(call(c,IOS27_22B_LSEEK,(uint64_t)fd,0,0))==0);
    assert(succeed(call(c,IOS27_22B_WRITE,(uint64_t)fd,128,3))==3);
    assert(succeed(call(c,IOS27_22B_LSEEK,(uint64_t)fd,0,0))==0);
    assert(succeed(call(c,IOS27_22B_READ,(uint64_t)fd,512,3))==3);
    assert(memcmp(mem->guest+512,"abc",3)==0);
    assert(succeed(call(c,IOS27_22B_LSEEK,(uint64_t)fd,4000,0))==4000);
    assert(succeed(call(c,IOS27_22B_READ,(uint64_t)fd,512,3))==0);
    assert(ios27_22b_metadata_fd(c,fd,&meta)==0 && meta.position==4000);
    /* ABI iovec: 64-bit base and size (not Linux-host struct pointers). */
    { uint64_t iov[4]={128,2,130,1};memcpy(mem->guest+704,iov,sizeof(iov)); }
    assert(succeed(call(c,IOS27_22B_LSEEK,(uint64_t)fd,0,0))==0);
    assert(succeed(call(c,IOS27_22B_WRITEV,(uint64_t)fd,704,2))==3);
    assert(succeed(call(c,IOS27_22B_LSEEK,(uint64_t)fd,0,0))==0);
    memset(mem->guest+800,0,3);
    { uint64_t iov[4]={800,2,802,1};memcpy(mem->guest+704,iov,sizeof(iov)); }
    assert(succeed(call(c,IOS27_22B_READV,(uint64_t)fd,704,2))==3);
    assert(memcmp(mem->guest+800,"abc",3)==0);
    /* Transactional writev must not advance fd or mutate file on fault. */
    assert(succeed(call(c,IOS27_22B_LSEEK,(uint64_t)fd,0,0))==0);
    { uint64_t iov[4]={128,2,UINT64_MAX,1};memcpy(mem->guest+704,iov,sizeof(iov)); }
    fails(call(c,IOS27_22B_WRITEV,(uint64_t)fd,704,2),IOS27_22B_EFAULT);
    assert(ios27_22b_metadata_fd(c,fd,&meta)==0 && meta.position==0);
    assert(succeed(call(c,IOS27_22B_READ,(uint64_t)fd,800,3))==3);
    assert(memcmp(mem->guest+800,"abc",3)==0);
    fails(call(c,IOS27_22B_WRITE,(uint64_t)fd,UINT64_MAX,1),IOS27_22B_EFAULT);
    fails(call(c,IOS27_22B_READ,(uint64_t)fd,32768,8),IOS27_22B_EFAULT);
    fails(call(c,IOS27_22B_WRITEV,(uint64_t)fd,705,1),IOS27_22B_EFAULT);
    fails(call(c,IOS27_22B_READV,(uint64_t)fd,704,17),IOS27_22B_EINVAL);
    fails(call(c,IOS27_22B_OPEN,UINT64_MAX,0,0),IOS27_22B_EFAULT);
    strcpy((char*)mem->guest+256,"/cap/readonly");
    fails(call(c,IOS27_22B_OPEN,256,IOS27_22B_O_WRONLY,0),IOS27_22B_EACCES);
    strcpy((char*)mem->guest+256,"/etc/passwd");
    fails(call(c,IOS27_22B_OPEN,256,0,0),IOS27_22B_EACCES);
    fails(call(c,IOS27_22B_LSEEK,(uint64_t)fd,UINT64_MAX,0),IOS27_22B_EINVAL);
    fails(call(c,IOS27_22B_WRITE,(uint64_t)fd,128,5000),IOS27_22B_EINVAL);
    fails(call(c,IOS27_22B_WRITE,15,128,1),IOS27_22B_EBADF);
    fails(call(c,IOS27_22B_OPEN,256,UINT64_MAX,0),IOS27_22B_EINVAL);
    fails(call(c,899,0,0,0),IOS27_22B_ENOSYS);
    fails(call(c,232,0,0,0),IOS27_22B_ENOSYS);
    fails(call(c,240,0,0,0),IOS27_22B_ENOSYS);
    a[0]=0;fails(SYS(c,55,a),IOS27_22B_ENOSYS);
    fails(ios27_22b_dispatch(c,1,a),IOS27_22B_ENOSYS);
    assert(ios27_22b_errno(c)==IOS27_22B_ENOSYS);
    assert(succeed(call(c,IOS27_22B_GETPID,0,0,0))==1234);
    assert(ios27_22b_errno(c)==IOS27_22B_ENOSYS); /* success preserves errno */
    assert(succeed(call(c,IOS27_22B_GETUID,0,0,0))==501);
    assert(succeed(call(c,IOS27_22B_GETEGID,0,0,0))==20);
    assert(ios27_22b_set_clock(c,UINT64_C(1700000000123456789),100)==0);
    assert(succeed(private_call(c,IOS27_22B_CLOCK_GETTIME,0,960))==0);
    { int64_t times[2]={0};memcpy(times,mem->guest+960,16);
      assert(times[0]==1700000000 && times[1]==123456789); }
    assert(succeed(private_call(c,IOS27_22B_CLOCK_GETTIME,6,960))==0);
    { int64_t times[2]={0};memcpy(times,mem->guest+960,16);
      assert(times[0]==0 && times[1]==100); }
    assert(succeed(call(c,IOS27_22B_GETTIMEOFDAY,960,0,0))==0);
    { int64_t times[2]={0};memcpy(times,mem->guest+960,16);
      assert(times[1]==123456); }
    fails(call(c,IOS27_22B_GETTIMEOFDAY,960,1,0),IOS27_22B_ENOSYS);
    fails(call(c,IOS27_22B_GETTIMEOFDAY,960,0,1),IOS27_22B_ENOSYS);
    fails(private_call(c,IOS27_22B_CLOCK_GETTIME,111,960),IOS27_22B_EINVAL);
    fails(private_call(c,IOS27_22B_CLOCK_GETTIME,6,961),IOS27_22B_EFAULT);
    { int64_t times[2]={1,200};memcpy(mem->guest+960,times,16); }
    ios27_22b_interrupt(c);
    fails(private_call(c,IOS27_22B_NANOSLEEP,960,992),IOS27_22B_EINTR);
    assert(memcmp(mem->guest+960,mem->guest+992,16)==0);
    assert(succeed(private_call(c,IOS27_22B_NANOSLEEP,960,992))==0);
    assert(ios27_22b_advance(c,300)==0);
    assert(succeed(private_call(c,IOS27_22B_CLOCK_GETTIME,6,1008))==0);
    { int64_t t[2];memcpy(t,mem->guest+1008,16);assert(t[0]==1 && t[1]==600); }
    assert(succeed(call(c,IOS27_22B_THREAD_SELFID,0,0,0))>=1001);
    assert(succeed(call(c,IOS27_22B_CLOSE,(uint64_t)fd,0,0))==0);
    fails(call(c,IOS27_22B_CLOSE,(uint64_t)fd,0,0),IOS27_22B_EBADF);
    ios27_22b_destroy(c);free(mem);
}
static void stress(void) {
    ios27_20e_ctx *mem=(ios27_20e_ctx*)calloc(1,sizeof(*mem));
    ios27_22b_context *c=NULL;pthread_t threads[4];worker workers[4];
    assert(mem);ios27_20e_init(mem);
    assert(ios27_22b_create(&c,mem,1234,501,20)==0);
    assert(ios27_22b_preopen(c,"alpha","abcdefghijklmnop",16,0)==0);
    strcpy((char*)mem->guest+256,"/cap/alpha");
    for(unsigned i=0;i<4u;++i) {
        workers[i].ctx=c;workers[i].mem=mem;workers[i].id=i;
        workers[i].fd=(int)succeed(call(c,IOS27_22B_OPEN,256,0,0));
        assert(pthread_create(&threads[i],NULL,thread_run,&workers[i])==0);
    }
    for(unsigned i=0;i<4u;++i)assert(pthread_join(threads[i],NULL)==0);
    for(unsigned i=0;i<4u;++i)assert(succeed(call(c,IOS27_22B_CLOSE,(uint64_t)workers[i].fd,0,0))==0);
    const int fault_fd=(int)succeed(call(c,IOS27_22B_OPEN,256,0,0));
    uint32_t random_state=UINT32_C(0x22b180);
    for(unsigned i=0;i<30000u;++i) {
        random_state^=random_state<<13;
        random_state^=random_state>>17;
        random_state^=random_state<<5;
        uint64_t bad=(random_state&1u)?UINT64_MAX:(uint64_t)32768+(random_state&4095u);
        fails(call(c,IOS27_22B_READ,(uint64_t)fault_fd,bad,1),IOS27_22B_EFAULT);
    }
    assert(succeed(call(c,IOS27_22B_CLOSE,(uint64_t)fault_fd,0,0))==0);
    for(unsigned i=0;i<60000u;++i) {
        int fd=(int)succeed(call(c,IOS27_22B_OPEN,256,0,0));
        assert(succeed(call(c,IOS27_22B_READ,(uint64_t)fd,2048,16))==16);
        assert(succeed(call(c,IOS27_22B_CLOSE,(uint64_t)fd,0,0))==0);
    }
    { int opened[IOS27_22B_FDS]={0};
      for(unsigned i=3;i<IOS27_22B_FDS;++i)opened[i]=(int)succeed(call(c,IOS27_22B_OPEN,256,0,0));
      fails(call(c,IOS27_22B_OPEN,256,0,0),IOS27_22B_EMFILE);
      for(unsigned i=3;i<IOS27_22B_FDS;++i)assert(succeed(call(c,IOS27_22B_CLOSE,(uint64_t)opened[i],0,0))==0);
    }
    ios27_22b_destroy(c);free(mem);
}
int main(void) {
    for(unsigned i=0;i<20u;++i)base_test();
    stress();
    puts("phase22b: passed: >450k dispatches, 0x22b180 seeded faults, concurrency, teardown");
    return 0;
}