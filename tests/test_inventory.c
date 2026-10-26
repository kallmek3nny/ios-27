#include "ios27/inventory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static void le32(uint8_t *p, uint32_t v) {
    for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(v>>(i*8u));
}
static void be32(uint8_t *p, uint32_t v) {
    for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(v>>(24u-i*8u));
}
static void be64(uint8_t *p, uint64_t v) {
    be32(p,(uint32_t)(v>>32)); be32(p+4,(uint32_t)v);
}
static size_t make_thin(uint8_t *p,size_t capacity) {
    const char *name="/System/Library/Frameworks/UIKit.framework/UIKit";
    size_t n=strlen(name)+1u;
    size_t cmdsize=(24u+n+7u)&~7u;
    assert(capacity>=32u+cmdsize);
    memset(p,0,capacity);
    le32(p,0xfeedfacfu); le32(p+4,0x0100000cu);
    le32(p+8,0u); le32(p+12,2u); le32(p+16,1u);
    le32(p+20,(uint32_t)cmdsize);
    le32(p+32,0x0cu); le32(p+36,(uint32_t)cmdsize);
    le32(p+40,24u); memcpy(p+56,name,n);
    return 32u+cmdsize;
}

int main(void) {
    uint8_t thin[256], fat[1024], broken[256];
    size_t thin_len=make_thin(thin,sizeof(thin));
    ios27_inventory inv;
    char err[128];
    assert(ios27_inventory_parse(thin,thin_len,&inv,err,sizeof(err))==0);
    assert(inv.imports_count==1u);
    assert(inv.frameworks_count==1u && inv.private_frameworks_count==0u);
    assert(strcmp(inv.imports[0].path,"/System/Library/Frameworks/UIKit.framework/UIKit")==0);
    assert(!inv.is_fat && inv.slice_offset==0u && inv.slice_size==thin_len);
    assert(ios27_inventory_native_ui_ready(&inv)==0);

    /* FAT_MAGIC, prefer generic ARM64 over arm64e when both are present. */
    memset(fat,0,sizeof(fat));
    be32(fat,0xcafebabeu); be32(fat+4,2u);
    be32(fat+8,0x0100000cu); be32(fat+12,2u);
    be32(fat+16,256u); be32(fat+20,(uint32_t)thin_len); be32(fat+24,2u);
    be32(fat+28,0x0100000cu); be32(fat+32,0u);
    be32(fat+36,512u); be32(fat+40,(uint32_t)thin_len); be32(fat+44,2u);
    memcpy(fat+256,thin,thin_len);
    memcpy(fat+512,thin,thin_len);
    assert(ios27_inventory_parse(fat,sizeof(fat),&inv,err,sizeof(err))==0);
    assert(inv.is_fat && inv.slice_offset==512u && !inv.is_arm64e);

    /* FAT_MAGIC_64 with 64-bit offsets. */
    memset(fat,0,sizeof(fat));
    be32(fat,0xcafebabfu); be32(fat+4,1u);
    be32(fat+8,0x0100000cu); be32(fat+12,0u);
    be64(fat+16,512u); be64(fat+24,thin_len); be32(fat+32,3u);
    memcpy(fat+512,thin,thin_len);
    assert(ios27_inventory_parse(fat,sizeof(fat),&inv,err,sizeof(err))==0);
    assert(inv.slice_offset==512u && inv.is_fat);
    be64(fat+24,UINT64_MAX);
    assert(ios27_inventory_parse(fat,sizeof(fat),&inv,err,sizeof(err))!=0);

    memcpy(broken,thin,thin_len);
    le32(broken+40,0xffffffffu);
    assert(ios27_inventory_parse(broken,thin_len,&inv,err,sizeof(err))!=0);
    memcpy(broken,thin,thin_len);
    broken[56]=0;
    assert(ios27_inventory_parse(broken,thin_len,&inv,err,sizeof(err))!=0);
    memcpy(broken,thin,thin_len);
    memset(broken+56,'x',thin_len-56u);
    assert(ios27_inventory_parse(broken,thin_len,&inv,err,sizeof(err))!=0);
    puts("inventory tests passed");
    return 0;
}
