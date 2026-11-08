#include "ios27/chained.h"
#include "ios27/process.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t r32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static uint64_t r64(const uint8_t *p) { return r32(p) | ((uint64_t)r32(p+4)<<32); }
static void w32(uint8_t *p, uint32_t v) { for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(v>>(8*i)); }
static void w64(uint8_t *p, uint64_t v) { for (unsigned i=0;i<8;++i) p[i]=(uint8_t)(v>>(8*i)); }
static void rejected(const uint8_t *bytes, size_t size) {
    ios27_macho_info info;
    ios27_chained_plan plan;
    static const ios27_chained_plan zero;
    char error[256];
    assert(!ios27_macho_parse(bytes,size,&info,error,sizeof(error)));
    memset(&plan,0xa5,sizeof(plan));
    assert(ios27_chained_static_plan(bytes,size,&info,&plan,error,sizeof(error))==-1);
    assert(!memcmp(&plan,&zero,sizeof(plan)));
}
int main(int argc, char **argv) {
    FILE *f;
    long length;
    uint8_t *bytes,*copy,*before;
    size_t size, cursor, payload=0, first;
    ios27_macho_info info;
    ios27_exec_layout layout;
    ios27_chained_plan plan;
    char error[256];
    if (argc==2 && !strcmp(argv[1],"--no-linker")) return 77;
    assert(argc==3);
    f=fopen(argv[1],"rb"); assert(f);
    assert(!fseek(f,0,SEEK_END)); length=ftell(f); assert(length>=32 && length<=2097152);
    size=(size_t)length; rewind(f);
    bytes=malloc(size); copy=malloc(size); before=malloc(size); assert(bytes && copy && before);
    assert(fread(bytes,1,size,f)==size); fclose(f); memcpy(before,bytes,size);
    assert(!ios27_macho_parse(bytes,size,&info,error,sizeof(error)));
    assert(!ios27_macho_executable_layout(bytes,size,&info,4096,&layout,error,sizeof(error)));
    assert(!ios27_macho_executable_layout(bytes,size,&info,16384,&layout,error,sizeof(error)));
    assert(!ios27_chained_static_plan(bytes,size,&info,&plan,error,sizeof(error)));
    assert(plan.count && !plan.import_count);
    memcpy(copy,bytes,size);
    assert(!ios27_chained_apply(copy,size,&info,&plan,UINT64_C(0x120000000),NULL,NULL,error,sizeof(error)));
    for (size_t i=0;i<plan.count;++i) {
        const ios27_chained_patch *p=&plan.patches[i];
        size_t off=(size_t)(info.segments[p->segment_index].fileoff+p->segment_offset);
        assert(r64(copy+off)==UINT64_C(0x120000000)+p->rebase_target);
    }
    assert(!memcmp(before,bytes,size));
    {
        uint8_t *snapshot=malloc(size); assert(snapshot);
        static const ios27_chained_plan zero;
        for (unsigned i=0;i<128;++i) {
            ios27_macho_info changed=info;
            ios27_chained_plan candidate;
            memcpy(copy,bytes,size);
            size_t at=32u+((size_t)i*97u % r32(bytes+20));
            copy[at]^=(uint8_t)(1u<<(i%8u));
            memcpy(snapshot,copy,size);
            (void)ios27_macho_parse(copy,size,&changed,error,sizeof(error));
            memset(&candidate,0xa5,sizeof(candidate));
            int status=ios27_chained_static_plan(copy,size,&changed,&candidate,error,sizeof(error));
            assert(status==0 || status==-1);
            if (status) assert(!memcmp(&candidate,&zero,sizeof(candidate)));
            assert(!memcmp(copy,snapshot,size));
        }
        free(snapshot);
    }
    first=(size_t)(info.segments[plan.patches[0].segment_index].fileoff+plan.patches[0].segment_offset);
    memcpy(copy,bytes,size); w64(copy+first,r64(copy+first)|(UINT64_C(1)<<63)); rejected(copy,size);
    memcpy(copy,bytes,size); w64(copy+first,UINT64_MAX); rejected(copy,size);
    cursor=32;
    for (uint32_t i=0;i<r32(bytes+16);++i) {
        if (r32(bytes+cursor)==0x80000034u) payload=r32(bytes+cursor+8);
        if (r32(bytes+cursor)==0x19u && r64(bytes+cursor+24)==info.segments[plan.patches[0].segment_index].vmaddr) {
            memcpy(copy,bytes,size); w32(copy+cursor+60,1); rejected(copy,size);
            for (uint32_t j=0;j<r32(bytes+cursor+64);++j) {
                size_t section=cursor+72+(size_t)j*80;
                uint64_t start=r64(bytes+section+32), count=r64(bytes+section+40);
                uint64_t address=info.segments[plan.patches[0].segment_index].vmaddr+plan.patches[0].segment_offset;
                if (address>=start && address-start<count) {
                    memcpy(copy,bytes,size); w32(copy+section+64,1); rejected(copy,size);
                }
            }
        }
        cursor+=r32(bytes+cursor+4);
    }
    assert(payload); memcpy(copy,bytes,size); w32(copy+payload+16,1); rejected(copy,size);
    {
        ios27_chained_plan empty;
        static const ios27_chained_plan zero;
        memcpy(copy,bytes,size);
        w32(copy+payload+r32(copy+payload+4),0);
        w32(copy+payload+20,0); /* Earlier linkers' inert format-0 payload. */
        memset(&empty,0xa5,sizeof(empty)); memset(error,'x',sizeof(error));
        assert(!ios27_chained_static_plan(copy,size,&info,&empty,error,sizeof(error)));
        assert(!memcmp(&empty,&zero,sizeof(empty)) && !error[0]);
    }
#if defined(__aarch64__) && defined(__linux__)
    {
        ios27_process_options options={2000};
        ios27_startup_context context={0,NULL,NULL,NULL};
        ios27_process_result result;
        ios27_macho_info bad_info;
        memcpy(copy,bytes,size); w32(copy+payload+16,1);
        assert(!ios27_macho_parse(copy,size,&bad_info,error,sizeof(error)));
        assert(ios27_run_executable_exec(argv[2],copy,size,&bad_info,&context,&options,&result,error,sizeof(error))==-1);
        assert(!result.outcome && !result.exit_code && !result.signal);
        for (unsigned i=0;i<3;++i) {
            assert(!ios27_run_executable_exec(argv[2],bytes,size,&info,&context,&options,&result,error,sizeof(error)));
            assert(result.outcome==IOS27_PROCESS_RETURNED && result.exit_code==42);
        }
    }
#endif
    free(before);free(copy);free(bytes);
    puts("static rebases: real compiler pointers, private relocation, no imports, read-only/zero-fill refusal and cleared failures passed");
    return 0;
}
