#ifndef IOS27_OBJC20A_METADATA_H
#define IOS27_OBJC20A_METADATA_H
#include <stddef.h>
#include <stdint.h>

/* Bounded metadata for genuine clang ARM64 Mach-O MH_OBJECT files.
 * Inspection never calls guest code or dereferences guest pointers. */
#define IOS27_OBJC20A_NAME 96u
#define IOS27_OBJC20A_CLASSES 32u
#define IOS27_OBJC20A_PROTOCOLS 32u
#define IOS27_OBJC20A_CATEGORIES 32u
#define IOS27_OBJC20A_METHODS 32u
#define IOS27_OBJC20A_IVARS 32u
#define IOS27_OBJC20A_PROPERTIES 32u
#define IOS27_OBJC20A_ADOPTIONS 16u
#define IOS27_OBJC20A_SELECTORS 128u

typedef struct {
    char name[IOS27_OBJC20A_NAME];
    char encoding[IOS27_OBJC20A_NAME];
    uint64_t implementation;  /* MH_OBJECT section-relative vm address */
    uint8_t class_method;
    uint8_t optional;
} ios27_objc20a_method;
typedef struct {
    char name[IOS27_OBJC20A_NAME];
    char encoding[IOS27_OBJC20A_NAME];
    uint32_t offset;   /* initial non-fragile offset cell value, not patched */
    uint32_t size;
    uint32_t alignment_log2;
} ios27_objc20a_ivar;
typedef struct {
    char name[IOS27_OBJC20A_NAME];
    char attributes[IOS27_OBJC20A_NAME];
} ios27_objc20a_property;
typedef struct {
    char name[IOS27_OBJC20A_NAME];
    char superclass[IOS27_OBJC20A_NAME];
    uint32_t instance_size;
    uint32_t method_count, ivar_count, property_count, protocol_count;
    ios27_objc20a_method methods[IOS27_OBJC20A_METHODS];
    ios27_objc20a_ivar ivars[IOS27_OBJC20A_IVARS];
    ios27_objc20a_property properties[IOS27_OBJC20A_PROPERTIES];
    char protocols[IOS27_OBJC20A_ADOPTIONS][IOS27_OBJC20A_NAME];
} ios27_objc20a_class;
typedef struct {
    char name[IOS27_OBJC20A_NAME];
    uint32_t method_count, property_count, protocol_count;
    ios27_objc20a_method methods[IOS27_OBJC20A_METHODS];
    ios27_objc20a_property properties[IOS27_OBJC20A_PROPERTIES];
    char protocols[IOS27_OBJC20A_ADOPTIONS][IOS27_OBJC20A_NAME];
} ios27_objc20a_protocol;
typedef struct {
    char name[IOS27_OBJC20A_NAME];
    char class_name[IOS27_OBJC20A_NAME];
    uint32_t method_count, property_count, protocol_count;
    ios27_objc20a_method methods[IOS27_OBJC20A_METHODS];
    ios27_objc20a_property properties[IOS27_OBJC20A_PROPERTIES];
    char protocols[IOS27_OBJC20A_ADOPTIONS][IOS27_OBJC20A_NAME];
} ios27_objc20a_category;
typedef struct {
    uint32_t class_count, category_count, protocol_count, selector_count;
    ios27_objc20a_class classes[IOS27_OBJC20A_CLASSES];
    ios27_objc20a_category categories[IOS27_OBJC20A_CATEGORIES];
    ios27_objc20a_protocol protocols[IOS27_OBJC20A_PROTOCOLS];
    char selectors[IOS27_OBJC20A_SELECTORS][IOS27_OBJC20A_NAME];
} ios27_objc20a_image;

/* Both functions clear destination on failure. No partial registry mutations. */
int ios27_objc20a_inspect(const uint8_t *file, size_t size,
                         ios27_objc20a_image *out, char *err, size_t cap);
typedef struct ios27_objc20a_registry ios27_objc20a_registry;
int ios27_objc20a_registry_create(ios27_objc20a_registry **out);
void ios27_objc20a_registry_destroy(ios27_objc20a_registry *registry);
int ios27_objc20a_register(ios27_objc20a_registry *registry,
                          const ios27_objc20a_image *image);
int ios27_objc20a_register_image(ios27_objc20a_registry *registry,
                                const uint8_t *file, size_t size,
                                char *err, size_t cap);
int ios27_objc20a_find_class(const ios27_objc20a_registry *registry,
                            const char *name, ios27_objc20a_class *out);
void ios27_objc20a_counts(const ios27_objc20a_registry *registry,
                         size_t *classes, size_t *protocols,
                         size_t *categories, size_t *selectors);
#endif
