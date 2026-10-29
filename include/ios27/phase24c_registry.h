#ifndef IOS27_PHASE24C_REGISTRY_H
#define IOS27_PHASE24C_REGISTRY_H
#include "ios27/objc20a_metadata.h"
#include <stddef.h>
#include <stdint.h>

/* Metadata and resolution only. NEVER cast a Mach-O VM offset to a host IMP.
 * Input is a validated ios27_objc20a_inspect result; no Apple libobjc ABI
 * execution, relocation, pointer authentication or guest address mapping. */
#define PHASE24C_MAX_IMAGES 16u
#define PHASE24C_MAX_CLASSES 512u

typedef struct phase24c_registry phase24c_registry;
typedef enum {
    PHASE24C_UNSUPPORTED = 0,
    PHASE24C_VOID0, PHASE24C_WORD0, PHASE24C_WORD1,
    PHASE24C_WORD2, PHASE24C_WORD3, PHASE24C_WORD4,
    PHASE24C_FLOAT1, PHASE24C_DOUBLE1
} phase24c_abi;
typedef struct {
    ios27_objc20a_method method; /* Section-relative address: NOT callable. */
    phase24c_abi abi;
    uint32_t image_index;
    uint8_t category;
} phase24c_resolution;
typedef struct {
    uint64_t registrations, rejections, lookups, unresolved_superclasses;
    size_t images, classes, categories, protocols;
} phase24c_stats;

/* The parser must validate Mach-O format/relocations before registration. */
int phase24c_create(phase24c_registry **out);
void phase24c_destroy(phase24c_registry *registry); /* caller quiesces users */
int phase24c_register(phase24c_registry *registry,
                      const ios27_objc20a_image *verified_image);
/* -2: unresolved external superclass; -1: absent/invalid; 0: found.
 * lexical_class is the class containing the [super ...] expression,
 * NOT the receiver's dynamic class. NULL selects normal dispatch. */
int phase24c_resolve(phase24c_registry *registry,
                     const char *dynamic_class, const char *selector,
                     int class_method, const char *lexical_class,
                     phase24c_resolution *out);
phase24c_abi phase24c_encoding(const char *encoding);
int phase24c_statistics(phase24c_registry *registry, phase24c_stats *out);
#endif
