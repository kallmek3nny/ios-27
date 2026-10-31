#define _POSIX_C_SOURCE 200809L
#include "ios27/phase24d_services.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1); } } while(0)
static ios27_24d_runtime *rt;
static uint64_t a,b,ta,tb,port,reply;
static _Atomic uint64_t checks;
static uint32_t randstate=0x9e3779b9u;
static uint32_t rnd(void){randstate^=randstate<<13;randstate^=randstate>>17;randstate^=randstate<<5;return randstate;}
static ios27_24d_result call(uint64_t pid,uint64_t tid,uint32_t sys,
                            uint64_t x0,uint64_t x1,uint64_t x2,uint64_t x3) {
    ios27_24d_frame f={.abi=IOS27_24D_ABI,.number=sys,.x={x0,x1,x2,x3,0,0}};
    ios27_24d_result r={0};
    CHECK(ios27_24d_trap(rt,pid,tid,&f,&r)==r.status);
    checks++;return r;
}
static void write_msg(uint64_t pid,uint64_t off,uint64_t corr,uint32_t op,uint32_t size,
                      uint64_t reply_port,uint64_t transfer_port) {
    ios27_24d_message m={.correlation=corr,.opcode=op,.size=size,
                         .reply_port=reply_port,.transfer_port=transfer_port};
    for (unsigned i=0;i<IOS27_24D_PAYLOAD;i++) m.payload[i]=(uint8_t)(i+op);
    CHECK(ios27_24d_memory_write(rt,pid,off,&m,sizeof(m))==0);
}
static void expect_sys(uint64_t pid,uint64_t tid,uint32_t sys,uint64_t x0,uint64_t x1,
                       uint64_t x2,uint64_t x3,int expected) {
    ios27_24d_result r=call(pid,tid,sys,x0,x1,x2,x3);
    if (r.status!=expected) {fprintf(stderr,"sys %u expected %d got %d\n",sys,expected,r.status);exit(1);}
}
typedef struct { uint64_t pid,tid,port;int receive,rc;uint32_t delay_ms; } work;
static void *worker(void *ptr) {
    work *w=ptr;
    ios27_24d_result r=call(w->pid,w->tid,w->receive?IOS27_24D_SYS_IPC_RECV:IOS27_24D_SYS_IPC_SEND,
                            w->port,w->receive?768:512,sizeof(ios27_24d_message),w->delay_ms);
    w->rc=r.status;return NULL;
}
static void delay_us(long us) {
    struct timespec t={.tv_sec=0,.tv_nsec=us*1000};(void)nanosleep(&t,NULL);
}
static void fd_check(void) {
    CHECK(ios27_24d_seed_file(rt,a,"/fixture/shared","abcdef",6)==0);
    const char *path="/fixture/shared";
    CHECK(ios27_24d_memory_write(rt,a,128,path,strlen(path)+1)==0);
    ios27_24d_result op=call(a,ta,IOS27_24D_SYS_OPEN,128,IOS27_24D_OPEN_READ|IOS27_24D_OPEN_WRITE,0,0);
    CHECK(op.status==0 && op.value>=0);
    uint64_t fd=(uint64_t)op.value;
    ios27_24d_result dup=call(a,ta,IOS27_24D_SYS_DUP,fd,0,0,0);
    CHECK(dup.status==0 && dup.value!=op.value);
    CHECK(call(a,ta,IOS27_24D_SYS_READ,fd,256,2,0).value==2);
    CHECK(call(a,ta,IOS27_24D_SYS_READ,(uint64_t)dup.value,260,2,0).value==2);
    char got[3]={0};CHECK(ios27_24d_memory_read(rt,a,260,got,2)==0 && !memcmp(got,"cd",2));
    CHECK(call(a,ta,IOS27_24D_SYS_LSEEK,fd,0,0,0).value==0);
    CHECK(ios27_24d_memory_write(rt,a,300,"XY",2)==0);
    CHECK(call(a,ta,IOS27_24D_SYS_WRITE,(uint64_t)dup.value,300,2,0).value==2);
    CHECK(call(a,ta,IOS27_24D_SYS_FD_FLAGS,fd,1,1,0).value==1);
    CHECK(call(a,ta,IOS27_24D_SYS_FD_FLAGS,(uint64_t)dup.value,0,0,0).value==0);
    CHECK(call(a,ta,IOS27_24D_SYS_DUP2,fd,(uint64_t)dup.value,0,0).status==0);
    CHECK(call(a,ta,IOS27_24D_SYS_CLOSE,fd,0,0,0).status==0);
    CHECK(call(a,ta,IOS27_24D_SYS_LSEEK,(uint64_t)dup.value,0,0,0).status==0);
    CHECK(call(a,ta,IOS27_24D_SYS_READ,(uint64_t)dup.value,300,2,0).value==2);
    CHECK(ios27_24d_memory_read(rt,a,300,got,2)==0 && !memcmp(got,"XY",2));
    expect_sys(a,ta,IOS27_24D_SYS_READ,fd,300,2,0,IOS27_24D_INVALID);
    CHECK(call(a,ta,IOS27_24D_SYS_CLOSE,(uint64_t)dup.value,0,0,0).status==0);
    CHECK(ios27_24d_memory_protect(rt,a,1,IOS27_24D_READ)==0);
    expect_sys(a,ta,IOS27_24D_SYS_OPEN,4095,IOS27_24D_OPEN_READ,0,0,IOS27_24D_FAULT);
    char untouched[6];
    CHECK(ios27_24d_memory_read(rt,a,4090,untouched,sizeof(untouched))==0);
    CHECK(ios27_24d_memory_write(rt,a,4090,"1234567890",10)==IOS27_24D_FAULT);
    char again[6];
    CHECK(ios27_24d_memory_read(rt,a,4090,again,sizeof(again))==0);
    CHECK(!memcmp(untouched,again,sizeof(again)));
    CHECK(ios27_24d_memory_write(rt,a,4095,"99",2)==IOS27_24D_FAULT);
    CHECK(ios27_24d_memory_protect(rt,a,1,IOS27_24D_READ|IOS27_24D_WRITE)==0);
    CHECK(ios27_24d_memory_read(rt,b,0,got,2)==IOS27_24D_FAULT);
    expect_sys(a,ta,99999,0,0,0,0,IOS27_24D_UNSUPPORTED);
    ios27_24d_frame bad={.abi=0,.number=IOS27_24D_SYS_GETPID};ios27_24d_result out;
    CHECK(ios27_24d_trap(rt,a,ta,&bad,&out)==IOS27_24D_BAD_ABI);
}
static void rpc_check(void) {
    CHECK(ios27_24d_port_allocate(rt,b,&port)==0);
    CHECK(ios27_24d_port_allocate(rt,a,&reply)==0);
    CHECK(ios27_24d_port_grant(rt,b,port,a,0)==0);
    write_msg(a,512,1337,1,16,reply,0);
    CHECK(call(a,ta,IOS27_24D_SYS_IPC_SEND,port,512,160,0).status==0);
    CHECK(call(b,tb,IOS27_24D_SYS_IPC_RECV,port,768,160,0).correlation==1337);
    ios27_24d_message readback;
    CHECK(ios27_24d_memory_read(rt,b,768,&readback,sizeof(readback))==0);
    CHECK(readback.reply_port==reply && readback.payload[0]==1);
    write_msg(b,512,1337,2,8,0,0);
    CHECK(call(b,tb,IOS27_24D_SYS_IPC_SEND,reply,512,160,0).status==0);
    CHECK(call(a,ta,IOS27_24D_SYS_IPC_RECV,reply,768,160,0).correlation==1337);
    /* Strict reply correlation and message consumption on errors. */
    write_msg(a,512,1338,1,16,reply,0);
    CHECK(call(a,ta,IOS27_24D_SYS_IPC_SEND,port,512,160,0).status==0);
    CHECK(call(b,tb,IOS27_24D_SYS_IPC_RECV,port,768,160,0).status==0);
    write_msg(b,512,1339,2,8,0,0);
    expect_sys(b,tb,IOS27_24D_SYS_IPC_SEND,reply,512,160,0,IOS27_24D_DENIED);
    write_msg(b,512,1338,2,8,0,0);
    CHECK(call(b,tb,IOS27_24D_SYS_IPC_SEND,reply,512,160,0).status==0);
    CHECK(call(a,ta,IOS27_24D_SYS_IPC_RECV,reply,768,160,0).status==0);
    /* Rights passed by successful enqueue, not by failed queue admission. */
    uint64_t extra;CHECK(ios27_24d_port_allocate(rt,a,&extra)==0);
    write_msg(a,512,1500,3,0,0,extra);
    CHECK(call(a,ta,IOS27_24D_SYS_IPC_SEND,port,512,160,0).status==0);
    CHECK(call(b,tb,IOS27_24D_SYS_IPC_RECV,port,768,160,0).status==0);
    write_msg(b,512,1501,3,0,0,0);
    CHECK(call(b,tb,IOS27_24D_SYS_IPC_SEND,extra,512,160,0).status==0);
    CHECK(call(a,ta,IOS27_24D_SYS_IPC_RECV,extra,768,160,0).status==0);
    CHECK(ios27_24d_port_destroy(rt,a,extra)==0);
    uint64_t death=0;CHECK(ios27_24d_poll_dead(rt,b,&death)==0 && death==extra);
}
static void nested_check(void) {
    uint64_t c,tc,inner,reply_b;
    CHECK(ios27_24d_process_create(rt,&c)==0);
    CHECK(ios27_24d_thread_create(rt,c,&tc)==0);
    CHECK(ios27_24d_port_allocate(rt,c,&inner)==0);
    CHECK(ios27_24d_port_allocate(rt,b,&reply_b)==0);
    CHECK(ios27_24d_port_grant(rt,c,inner,b,0)==0);
    /* A -> B -> C -> B -> A, with correlated one-shot replies. */
    write_msg(a,512,4000,1,4,reply,0);
    CHECK(call(a,ta,IOS27_24D_SYS_IPC_SEND,port,512,160,0).status==0);
    CHECK(call(b,tb,IOS27_24D_SYS_IPC_RECV,port,768,160,0).correlation==4000);
    write_msg(b,512,5000,2,4,reply_b,0);
    CHECK(call(b,tb,IOS27_24D_SYS_IPC_SEND,inner,512,160,0).status==0);
    CHECK(call(c,tc,IOS27_24D_SYS_IPC_RECV,inner,768,160,0).correlation==5000);
    write_msg(c,512,5000,3,4,0,0);
    CHECK(call(c,tc,IOS27_24D_SYS_IPC_SEND,reply_b,512,160,0).status==0);
    CHECK(call(b,tb,IOS27_24D_SYS_IPC_RECV,reply_b,768,160,0).correlation==5000);
    write_msg(b,512,4000,4,4,0,0);
    CHECK(call(b,tb,IOS27_24D_SYS_IPC_SEND,reply,512,160,0).status==0);
    CHECK(call(a,ta,IOS27_24D_SYS_IPC_RECV,reply,768,160,0).correlation==4000);
    CHECK(ios27_24d_process_destroy(rt,c)==0);
    CHECK(ios27_24d_port_destroy(rt,b,reply_b)==0);
}
/* Independent producer/consumer threads exchange fully compiled host calls. */
typedef struct { uint64_t pid,tid,port,addr;int recv;int errors; } stress_work;
static void *stress_worker(void *arg) {
    stress_work *w=(stress_work *)arg;
    for (unsigned i=0;i<4000;i++) {
        if (!w->recv) {
            ios27_24d_message m={.correlation=i+1,.opcode=7,.size=16};
            if (ios27_24d_memory_write(rt,w->pid,w->addr,&m,sizeof(m))) {w->errors++;break;}
        }
        ios27_24d_result r=call(w->pid,w->tid,
                               w->recv?IOS27_24D_SYS_IPC_RECV:IOS27_24D_SYS_IPC_SEND,
                               w->port,w->addr,160,1000);
        if (r.status) {w->errors++;break;}
        if (w->recv && r.correlation!=(uint64_t)(i+1)) {w->errors++;break;}
    }
    return NULL;
}
static void concurrent_check(void) {
    pthread_t workers[6];stress_work w[6]={0};
    for (unsigned j=0;j<3;j++) {
        uint64_t p;
        CHECK(ios27_24d_port_allocate(rt,b,&p)==0);
        CHECK(ios27_24d_port_grant(rt,b,p,a,0)==0);
        for (unsigned side=0;side<2;side++) {
            unsigned k=2*j+side;
            w[k].pid=side?b:a;w[k].port=p;w[k].recv=side;
            w[k].addr=2048+(uint64_t)j*512+(uint64_t)side*256;
            CHECK(ios27_24d_thread_create(rt,w[k].pid,&w[k].tid)==0);
        }
    }
    for (unsigned k=0;k<6;k++) CHECK(!pthread_create(&workers[k],NULL,stress_worker,&w[k]));
    for (unsigned k=0;k<6;k++) {
        CHECK(!pthread_join(workers[k],NULL));CHECK(!w[k].errors);
        CHECK(ios27_24d_thread_destroy(rt,w[k].pid,w[k].tid)==0);
    }
    for (unsigned j=0;j<3;j++) CHECK(ios27_24d_port_destroy(rt,b,w[2*j].port)==0);
}
static void race_check(void) {
    /* Fill exactly 8 entries to exercise bounded queue, then wake a writer. */
    for (unsigned i=0;i<IOS27_24D_QUEUE;i++) {
        write_msg(a,512,2000+i,1,0,0,0);
        CHECK(call(a,ta,IOS27_24D_SYS_IPC_SEND,port,512,160,0).status==0);
    }
    write_msg(a,512,3000,1,0,0,0);
    expect_sys(a,ta,IOS27_24D_SYS_IPC_SEND,port,512,160,0,IOS27_24D_TIMEOUT);
    uint64_t extra_thread;CHECK(ios27_24d_thread_create(rt,a,&extra_thread)==0);
    work w={.pid=a,.tid=extra_thread,.port=port,.delay_ms=1000};
    pthread_t id;CHECK(!pthread_create(&id,NULL,worker,&w));delay_us(15000);
    CHECK(call(b,tb,IOS27_24D_SYS_IPC_RECV,port,768,160,0).status==0);
    CHECK(!pthread_join(id,NULL) && w.rc==0);
    CHECK(ios27_24d_thread_destroy(rt,a,extra_thread)==0);
    while (call(b,tb,IOS27_24D_SYS_IPC_RECV,port,768,160,0).status==0) {}
    CHECK(ios27_24d_thread_create(rt,a,&extra_thread)==0);
    w=(work){.pid=a,.tid=extra_thread,.port=port,.receive=1,.delay_ms=1000};
    /* Owner B has the receive right, so B is the waiter. */
    uint64_t wait_thread;CHECK(ios27_24d_thread_create(rt,b,&wait_thread)==0);
    w.pid=b;w.tid=wait_thread;
    CHECK(!pthread_create(&id,NULL,worker,&w));delay_us(15000);
    CHECK(ios27_24d_thread_cancel(rt,b,wait_thread)==0);
    CHECK(!pthread_join(id,NULL) && w.rc==IOS27_24D_INTERRUPTED);
    CHECK(ios27_24d_thread_destroy(rt,b,wait_thread)==0);
    CHECK(ios27_24d_thread_destroy(rt,a,extra_thread)==0);
    uint64_t doomed;CHECK(ios27_24d_port_allocate(rt,b,&doomed)==0);
    CHECK(ios27_24d_thread_create(rt,b,&wait_thread)==0);
    w=(work){.pid=b,.tid=wait_thread,.port=doomed,.receive=1,.delay_ms=1000};
    CHECK(!pthread_create(&id,NULL,worker,&w));delay_us(15000);
    CHECK(ios27_24d_port_destroy(rt,b,doomed)==0);
    CHECK(!pthread_join(id,NULL) && w.rc==IOS27_24D_DEAD);
    CHECK(ios27_24d_thread_destroy(rt,b,wait_thread)==0);
}
static void fuzz_and_stress(void) {
    for (unsigned i=0;i<10000;i++) {
        ios27_24d_message m={0};
        m.correlation=(rnd()&1)?0:(uint64_t)(i+1);
        m.opcode=(rnd()&1)?0:1;
        m.size=IOS27_24D_PAYLOAD+1+(rnd()%100);
        CHECK(ios27_24d_memory_write(rt,a,512,&m,sizeof(m))==0);
        expect_sys(a,ta,IOS27_24D_SYS_IPC_SEND,port,512,160,0,IOS27_24D_INVALID);
    }
    for (unsigned i=0;i<85000;i++) {
        write_msg(a,512,(uint64_t)(10000+i),3,128,0,0);
        CHECK(call(a,ta,IOS27_24D_SYS_IPC_SEND,port,512,160,0).status==0);
        CHECK(call(b,tb,IOS27_24D_SYS_IPC_RECV,port,768,160,0).status==0);
        if (i%5==0) CHECK(call(a,ta,IOS27_24D_SYS_GETPID,0,0,0,0).value==(int64_t)a);
    }
}
int main(void) {
    CHECK(ios27_24d_new(&rt)==0);
    CHECK(ios27_24d_process_create(rt,&a)==0 && ios27_24d_process_create(rt,&b)==0);
    CHECK(ios27_24d_thread_create(rt,a,&ta)==0 && ios27_24d_thread_create(rt,b,&tb)==0);
    fd_check();rpc_check();nested_check();race_check();concurrent_check();fuzz_and_stress();
    uint64_t olda=a;
    CHECK(ios27_24d_process_destroy(rt,a)==0);
    CHECK(ios27_24d_process_destroy(rt,b)==0);
    uint64_t fresh;CHECK(ios27_24d_process_create(rt,&fresh)==0 && olda!=fresh);
    CHECK(ios27_24d_memory_write(rt,olda,512,"a",1)==IOS27_24D_DEAD);
    CHECK(ios27_24d_process_destroy(rt,fresh)==0);
    ios27_24d_stats s;ios27_24d_get_stats(rt,&s);
    CHECK(s.calls>=150000 && s.sends>=85000 && s.receives>=85000);
    CHECK(!s.active_tasks && !s.active_threads && !s.queued && s.cancelled>=1);
    printf("PASS phase24d host executed: calls=%llu sends=%llu receives=%llu fd=%llu copies=%llu "
           "rejected=%llu timed_out=%llu cancelled=%llu clean_ports=%llu checked=%llu\n",
           (unsigned long long)s.calls,(unsigned long long)s.sends,
           (unsigned long long)s.receives,(unsigned long long)s.fd_ops,
           (unsigned long long)s.copies,(unsigned long long)s.rejected,
           (unsigned long long)s.timed_out,(unsigned long long)s.cancelled,
           (unsigned long long)s.cleaned_ports,(unsigned long long)checks);
    ios27_24d_free(rt);return 0;
}
