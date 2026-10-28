#include "ios27/libsystem20e_services.h"

/* The same compiler-authored integration program is built as genuine Darwin
 * ARM64 MH_OBJECT and as a native-host object linked to the service layer.
 * Its Darwin object is not executed by Linux. */
int phase20e_compiled_client(ios27_20e_ctx *c) {
    static const char path[]="/tmp/compiled.log";
    static const char data[]="phase20e compiled client";
    uint64_t i, ptr; int64_t fd, mapped;
    for (i=0;i<sizeof(path);++i)c->guest[1024+i]=(uint8_t)path[i];
    for (i=0;i<sizeof(data)-1;++i)c->guest[1152+i]=(uint8_t)data[i];
    fd=ios27_20e_open(c,1024,IOS27_20E_RDWR|IOS27_20E_CREAT);
    if(fd<0)return 1;
    if(ios27_20e_write(c,(int)fd,1152,sizeof(data)-1)!=(int64_t)(sizeof(data)-1))return 2;
    if(ios27_20e_lseek(c,(int)fd,0,IOS27_20E_SEEK_SET)!=0)return 3;
    if(ios27_20e_read(c,(int)fd,1280,sizeof(data)-1)!=(int64_t)(sizeof(data)-1))return 4;
    for(i=0;i<sizeof(data)-1;++i)if(c->guest[1280+i]!=(uint8_t)data[i])return 5;
    if(ios27_20e_close(c,(int)fd)!=0)return 6;
    if(ios27_20e_gettimeofday(c,1408)!=0)return 7;
    ptr=ios27_20e_calloc(c,5,24);
    if(!ptr)return 8;
    for(i=0;i<120;++i)if(c->guest[ptr+i])return 9;
    if(ios27_20e_free(c,ptr)!=0)return 10;
    mapped=ios27_20e_mmap(c,0,8192,IOS27_20E_PROT_READ|IOS27_20E_PROT_WRITE,
                         IOS27_20E_MAP_PRIVATE|IOS27_20E_MAP_ANON,-1,0);
    if(mapped<0)return 11;
    if(!ios27_20e_guest_ptr(c,(uint64_t)mapped,8192,1))return 12;
    if(ios27_20e_mprotect(c,(uint64_t)mapped,8192,IOS27_20E_PROT_READ)!=0)return 13;
    if(ios27_20e_guest_ptr(c,(uint64_t)mapped,1,1))return 14;
    if(ios27_20e_munmap(c,(uint64_t)mapped,8192)!=0)return 15;
    return 0;
}