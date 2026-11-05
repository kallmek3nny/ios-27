#include "ios27/phase26b_bsd.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static ios27_phase26b_regs call(ios27_phase26b_process *p, uint64_t num,
                                uint64_t a, uint64_t b, uint64_t c,
                                uint64_t d, uint64_t e, uint64_t f) {
    ios27_phase26b_regs r = {{a,b,c,d,e,f,0,0},IOS27_26B_BSD_CLASS | num,
                              IOS27_26B_SVC,0};
    ios27_phase26b_trap(p, &r);
    return r;
}
#define C(p,n,a,b,c,d,e,f) call((p),(n),(a),(b),(c),(d),(e),(f))
static uint64_t success(ios27_phase26b_regs r) {
    assert(!r.carry); return r.x[0];
}
static void failure(ios27_phase26b_regs r, uint64_t code) {
    assert(r.carry && r.x[0] == code);
}
static void put(ios27_phase26b_process *p, size_t offset, const char *str) {
    assert(!ios27_phase26b_copyout(p, IOS27_26B_BASE + offset, str,
                                  strlen(str) + 1u));
}
static void basic(void) {
    ios27_phase26b_process *p = ios27_phase26b_create(4201u, 1234567u);
    char buffer[64] = {0};
    uint8_t stat[144] = {0}, tv[16] = {0};
    uint64_t fd, dupfd, area;
    ios27_phase26b_regs r;
    assert(p);
    put(p, 32u, "/app/readme.txt");
    fd = success(C(p,5u,IOS27_26B_BASE+32u,0,0,0,0,0));
    assert(success(C(p,3u,fd,IOS27_26B_BASE+2048u,32u,0,0,0))==21u);
    assert(!ios27_phase26b_copyin(p,IOS27_26B_BASE+2048u,buffer,21u));
    assert(!memcmp(buffer,"ios27 isolated guest\n",21u));
    assert(success(C(p,153u,fd,IOS27_26B_BASE+2048u,5u,0,0,0)) == 5u);
    assert(success(C(p,199u,fd,0,1,0,0,0))==21u);
    assert(success(C(p,189u,fd,IOS27_26B_BASE+3072u,0,0,0,0))==0u);
    assert(!ios27_phase26b_copyin(p,IOS27_26B_BASE+3072u,stat,sizeof(stat)));
    assert(stat[4] == 0xa4u && stat[5] == 0x81u && stat[96] == 21u);
    assert(success(C(p,6u,fd,0,0,0,0,0))==0u);
    failure(C(p,3u,fd,IOS27_26B_BASE+2048u,1,0,0,0),9u);
    put(p,32u,"/tmp/example");
    fd = success(C(p,463u,UINT64_MAX-1u,IOS27_26B_BASE+32u,0x202u,0600,0,0));
    assert(fd >= 3u);
    assert(!ios27_phase26b_copyout(p,IOS27_26B_BASE+2048u,"abcd",4u));
    assert(success(C(p,4u,fd,IOS27_26B_BASE+2048u,4u,0,0,0))==4u);
    assert(success(C(p,199u,fd,0,0,0,0,0))==0u);
    dupfd = success(C(p,92u,fd,0,0,0,0,0));
    assert(dupfd != fd);
    assert(success(C(p,3u,dupfd,IOS27_26B_BASE+2100u,2u,0,0,0)) == 2u);
    assert(success(C(p,3u,fd,IOS27_26B_BASE+2102u,2u,0,0,0)) == 2u);
    assert(!ios27_phase26b_copyin(p,IOS27_26B_BASE+2100u,buffer,4u));
    assert(!memcmp(buffer,"abcd",4u));
    assert(success(C(p,154u,fd,IOS27_26B_BASE+2048u,4u,6u,0,0)) == 4u);
    assert(success(C(p,199u,fd,0,1,0,0,0)) == 4u);
    assert(success(C(p,153u,fd,IOS27_26B_BASE+2100u,4u,6u,0,0)) == 4u);
    assert(!ios27_phase26b_copyin(p,IOS27_26B_BASE+2100u,buffer,4u));
    assert(!memcmp(buffer,"abcd",4u));
    assert(success(C(p,6u,fd,0,0,0,0,0))==0u);
    assert(success(C(p,6u,dupfd,0,0,0,0,0))==0u);
    failure(C(p,6u,fd,0,0,0,0,0),9u);
    fd = success(C(p,5u,IOS27_26B_BASE+32u,2u,0,0,0,0));
    assert(fd != dupfd && fd != 3u); /* generation-tagged reuse */
    failure(C(p,6u,dupfd,0,0,0,0,0),9u);
    assert(success(C(p,6u,fd,0,0,0,0,0))==0u);
    put(p,32u,"/tmp/../host");
    failure(C(p,5u,IOS27_26B_BASE+32u,0,0,0,0,0),13u);
    put(p,32u,"/etc/passwd");
    failure(C(p,5u,IOS27_26B_BASE+32u,0,0,0,0,0),2u);
    failure(C(p,5u,UINT64_MAX-10u,0,0,0,0,0),14u);
    failure(C(p,463u,0u,IOS27_26B_BASE+32u,0,0,0,0),45u);
    failure(C(p,197u,0,4096,7,0x1002u,UINT64_MAX,0),45u);
    area = success(C(p,197u,0,4096,3,0x1002u,UINT64_MAX,0));
    assert(area == IOS27_26B_BASE+16u*IOS27_26B_PAGE);
    assert(!ios27_phase26b_copyout(p,area,"a",1u));
    assert(success(C(p,74u,area,4096,1,0,0,0)) == 0u);
    assert(ios27_phase26b_copyout(p,area,"b",1u));
    assert(!ios27_phase26b_copyin(p,area,buffer,1u) && buffer[0]=='a');
    assert(success(C(p,74u,area,4096,0,0,0,0)) == 0u);
    assert(ios27_phase26b_copyin(p,area,buffer,1u));
    assert(success(C(p,74u,area,4096,3,0,0,0)) == 0u);
    assert(success(C(p,73u,area,4096,0,0,0,0)) == 0u);
    assert(ios27_phase26b_copyin(p,area,buffer,1u));
    assert(success(C(p,197u,0,4096,3,0x1002u,UINT64_MAX,0)) == area);
    assert(!ios27_phase26b_copyin(p,area,buffer,1u) && !buffer[0]);
    assert(success(C(p,116u,IOS27_26B_BASE+3072u,0,0,0,0,0))==0u);
    assert(!ios27_phase26b_copyin(p,IOS27_26B_BASE+3072u,tv,sizeof(tv)));
    assert(tv[0] == 0x87u && tv[1] == 0xd6u);
    assert(success(C(p,20u,0,0,0,0,0,0))==4201u);
    assert(success(C(p,24u,0,0,0,0,0,0))==501u);
    r = C(p,999u,0,0,0,0,0,0); failure(r,78u);
    r = C(p,5u,0,0,0,0,0,0);
    r.instruction = 0u; ios27_phase26b_trap(p,&r); failure(r,22u);
    ios27_phase26b_stop(p);
    failure(C(p,20u,0,0,0,0,0,0),89u);
    ios27_phase26b_destroy(p);
}
typedef struct { ios27_phase26b_process *p; unsigned loops; } job;
static void *worker(void *raw) {
    job *j = raw;
    for (unsigned i = 0; i < j->loops; ++i) {
        ios27_phase26b_regs r = C(j->p,20u,0,0,0,0,0,0);
        assert(!r.carry && r.x[0] == 9042u);
        r = C(j->p,25u,0,0,0,0,0,0);
        assert(!r.carry && r.x[0] == 501u);
    }
    return NULL;
}
static void stress(void) {
    ios27_phase26b_process *p = ios27_phase26b_create(9042u,0);
    pthread_t threads[4];
    job j = {p, 40000u};
    assert(p);
    for (size_t i = 0; i < 4u; ++i)
        assert(!pthread_create(&threads[i], NULL, worker, &j));
    for (size_t i = 0; i < 4u; ++i)
        assert(!pthread_join(threads[i], NULL));
    ios27_phase26b_destroy(p);
    printf("phase26b: 320000 concurrent BSD trap dispatches verified\n");
}

typedef struct { ios27_phase26b_process *p; uint64_t fd; } fd_job;
static void *fd_worker(void *raw) {
    fd_job *j = raw;
    for (unsigned i = 0u; i < 5000u; ++i) {
        uint64_t dup = success(C(j->p,92u,j->fd,0,0,0,0,0));
        assert(success(C(j->p,4u,dup,IOS27_26B_BASE+3500u,1u,0,0,0))==1u);
        assert(success(C(j->p,6u,dup,0,0,0,0,0))==0u);
    }
    return NULL;
}
static void descriptor_stress(void) {
    ios27_phase26b_process *p = ios27_phase26b_create(888u,0);
    uint64_t dup[31], fd;
    pthread_t threads[4];
    fd_job j;
    assert(p);
    put(p,32u,"/tmp/data");
    assert(!ios27_phase26b_copyout(p,IOS27_26B_BASE+3500u,"q",1u));
    fd = success(C(p,5u,IOS27_26B_BASE+32u,2u,0,0,0,0));
    j.p=p; j.fd=fd;
    for (size_t i=0u; i<4u; ++i)
        assert(!pthread_create(&threads[i],NULL,fd_worker,&j));
    for (size_t i=0u; i<4u; ++i)
        assert(!pthread_join(threads[i],NULL));
    assert(success(C(p,199u,fd,0,1u,0,0,0))==20000u);
    for (size_t i=0u; i<31u; ++i)
        dup[i]=success(C(p,92u,fd,0,0,0,0,0));
    failure(C(p,92u,fd,0,0,0,0,0),24u);
    for (size_t i=0u; i<31u; ++i)
        assert(success(C(p,6u,dup[i],0,0,0,0,0))==0u);
    failure(C(p,6u,dup[0],0,0,0,0,0),9u);
    assert(success(C(p,6u,fd,0,0,0,0,0))==0u);
    failure(C(p,4u,fd,IOS27_26B_BASE+3500u,1u,0,0,0),9u);
    assert(ios27_phase26b_copyout(p,IOS27_26B_BASE+16u*IOS27_26B_PAGE-1u,
                                    "aa",2u));
    ios27_phase26b_destroy(p);
    puts("phase26b: 60000 concurrent fd dup/write/close operations verified");
}

int main(void) {
    basic();
    stress();
    descriptor_stress();
    puts("phase26b: memory, fd, syscall, VM and concurrency checks passed");
    return 0;
}
