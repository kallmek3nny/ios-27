#ifndef IOS27_OBJC_METADATA_H
#define IOS27_OBJC_METADATA_H
#include <stddef.h>
#include <stdint.h>

/* Read actual Clang-produced ARM64 Darwin MH_OBJECT ObjC2 metadata. The
 * object remains unlinked: UNSIGNED64 relocation records provide symbolic
 * references. This is metadata inspection, NOT execution or dyld mapping. */
#define IOS27_OBJC_METADATA_CLASSES 32u
#define IOS27_OBJC_METADATA_METHODS 32u
#define IOS27_OBJC_METADATA_NAME 64u
typedef struct {
    char selector[IOS27_OBJC_METADATA_NAME];
    char type_encoding[IOS27_OBJC_METADATA_NAME];
    uint64_t implementation_vmaddr; /* MH_OBJECT section-local VM address */
} ios27_objc_metadata_method;
typedef struct {
    char name[IOS27_OBJC_METADATA_NAME];
    char superclass[IOS27_OBJC_METADATA_NAME];
    uint32_t instance_size;
    uint32_t method_count;
    ios27_objc_metadata_method methods[IOS27_OBJC_METADATA_METHODS];
} ios27_objc_metadata_class;
typedef struct {
    uint32_t class_count;
    uint32_t selector_count;
    ios27_objc_metadata_class classes[IOS27_OBJC_METADATA_CLASSES];
} ios27_objc_metadata;

/* Fail-closed on missing/overlapping metadata, unbounded strings, malformed
 * relocations, unknown ObjC layout, unsupported relative methods or PAC.
 * Caller may pass a diagnostic buffer. Output cleared on every failure. */
int ios27_objc_metadata_inspect(const uint8_t *bytes, size_t size,
                                ios27_objc_metadata *out,
                                char *error, size_t error_cap);
#endif
