/* Execute only the audited, relocation-free +[Phase24cRoot kind] method
 * from our own Clang+LLD-linked ARM64 Darwin dylib, never arbitrary IMPs.
 * On non-aarch64 hosts, still verify the real compiler's Mach-O bytes. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__aarch64__) && defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#define MUST(x) do { if (!(x)) { \
    (void)fprintf(stderr, "phase24c native verification failed at %d\n", __LINE__); \
    exit(1); \
} } while (0)
static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static uint64_t rd64(const unsigned char *p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p+4)<<32);
}
static int range(size_t size, uint64_t at, uint64_t n) {
    return at <= size && n <= size - at;
}
int main(int argc, char **argv) {
    MUST(argc == 2 && argv[1]);
    FILE *f = fopen(argv[1], "rb");
    MUST(f != NULL && !fseek(f, 0, SEEK_END));
    long length = ftell(f);
    MUST(length > 32 && length < 4L * 1024L * 1024L);
    MUST(!fseek(f, 0, SEEK_SET));
    size_t size = (size_t)length;
    unsigned char *buf = malloc(size);
    MUST(buf && fread(buf, 1u, size, f) == size && !fclose(f));
    MUST(rd32(buf) == 0xfeedfacfu && rd32(buf+4) == 0x0100000cu);
    MUST((rd32(buf+8) & 255u) == 0 && rd32(buf+12) == 6u);
    uint32_t commands = rd32(buf+16), command_bytes = rd32(buf+20);
    MUST(commands > 0 && commands <= 96 && range(size,32,command_bytes));
    size_t pos = 32, cmd_end = 32u+command_bytes;
    uint64_t text_vm = 0, text_size = 0;
    uint32_t text_file = 0, symoff = 0, nsyms = 0, stroff = 0, strsize = 0;
    unsigned text_section = 0, current_section = 0, texts = 0, symtabs = 0;
    for (uint32_t i=0; i<commands; ++i) {
        MUST(pos <= cmd_end && cmd_end - pos >= 8);
        uint32_t op = rd32(buf+pos), n = rd32(buf+pos+4);
        MUST(n >= 8 && !(n & 7u) && n <= cmd_end-pos);
        if (op == 0x19u) {
            MUST(n >= 72);
            uint32_t count = rd32(buf+pos+64);
            MUST(count <= (n-72u)/80u && count <= 64u-current_section);
            for (uint32_t j=0; j<count; ++j) {
                const unsigned char *sec=buf+pos+72u+80u*j;
                ++current_section;
                if (!memcmp(sec,"__text",6) && !memcmp(sec+16,"__TEXT",6)) {
                    MUST(++texts == 1);
                    text_section = current_section;
                    text_vm = rd64(sec+32);
                    text_size = rd64(sec+40);
                    text_file = rd32(sec+48);
                    MUST(text_size>=8 && text_size<=16384 &&
                         range(size,text_file,text_size) && !rd32(sec+60));
                }
            }
        } else if (op == 0x2u) {
            MUST(++symtabs == 1 && n == 24);
            symoff=rd32(buf+pos+8);nsyms=rd32(buf+pos+12);
            stroff=rd32(buf+pos+16);strsize=rd32(buf+pos+20);
        }
        pos+=n;
    }
    MUST(pos == cmd_end && texts == 1 && symtabs == 1);
    MUST(nsyms > 0 && nsyms < 8192 && strsize > 0 &&
         range(size,symoff,(uint64_t)nsyms*16u) && range(size,stroff,strsize));
    const char *const symbol = "+[Phase24cRoot kind]";
    uint64_t method=0;
    unsigned matches=0;
    for (uint32_t i=0; i<nsyms; ++i) {
        const unsigned char *entry = buf+symoff+16u*(size_t)i;
        uint32_t strx=rd32(entry);
        MUST(strx < strsize);
        const char *name = (const char *)buf+stroff+strx;
        size_t rem = strsize-strx;
        MUST(memchr(name,0,rem));
        if (!strcmp(name,symbol)) {
            MUST(++matches==1 && (entry[4]&0x0eu)==0x0eu &&
                 entry[5]==text_section);
            method=rd64(entry+8);
        }
    }
    MUST(matches==1 && method >= text_vm && (method&3u)==0u &&
         method-text_vm <= text_size-8u);
    uint64_t at=(uint64_t)text_file+(method-text_vm);
    MUST(range(size,at,8));
    /* An intentionally narrow, non-relocating compiler IMP: mov w0,#1; ret.
     * Fails closed if linker or compiler changes its instruction sequence. */
    MUST(rd32(buf+(size_t)at)==0x52800020u);
    MUST(rd32(buf+(size_t)at+4u)==0xd65f03c0u);
#if defined(__aarch64__) && defined(__linux__)
    long pagesize=sysconf(_SC_PAGESIZE);
    MUST(pagesize >= 4096 && pagesize <= 65536);
    size_t span=(size_t)pagesize*2u;
    void *mapping=mmap(NULL,span,PROT_READ|PROT_WRITE,
                       MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    MUST(mapping != MAP_FAILED);
    unsigned char *page=(unsigned char *)mapping+(size_t)pagesize;
    memcpy(page,buf+(size_t)at,8u);
    __builtin___clear_cache((char *)page,(char *)page+8);
    /* UBSan function-type checks inspect bytes immediately before indirect
     * code pointers. Keep a separate read-only zero preamble page mapped;
     * the guest method itself remains on its own RX, never writable page. */
    MUST(mprotect(mapping,(size_t)pagesize,PROT_READ)==0);
    MUST(mprotect(page,(size_t)pagesize,PROT_READ|PROT_EXEC)==0);
    uintptr_t ip=(uintptr_t)page;
    uint64_t (*imp)(void *,const void *)=NULL;
    MUST(sizeof(ip)==sizeof(imp));
    memcpy(&imp,&ip,sizeof(imp));
    volatile uint64_t total=0;
    for (unsigned i=0;i<160000u;++i) total+=imp(NULL,NULL);
    MUST(total==160000u);
    MUST(munmap(mapping,span)==0);
    puts("phase24c: 160000 direct native calls to compiler-linked Darwin +[Phase24cRoot kind]");
#else
    puts("phase24c: genuine linked Darwin method inspected; execution requires aarch64 Linux");
#endif
    free(buf);
    return 0;
}
