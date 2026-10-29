#ifndef IOS27_PHASE23C_BRIDGE_H
#define IOS27_PHASE23C_BRIDGE_H
#include "ios27/phase22d_runtime.h"
#include <stddef.h>
#include <stdint.h>
#define PHASE23C_IMAGES 16u
#define PHASE23C_IMAGE_NAME 80u
#define PHASE23C_IMAGE_CLASSES 32u
#define PHASE23C_HANDLES PHASE22D_OBJECTS
typedef struct phase23c_bridge phase23c_bridge;
typedef struct { uint16_t slot; uint64_t generation; } phase23c_image;
typedef struct {
    uint64_t image_imports, failed_imports, category_replacements;
    uint64_t sends, failed_sends, retains, releases, allocations;
    size_t images, tracked_objects, live_objects, active_sends;
    phase22d_stats substrate;
} phase23c_stats;
int phase23c_create(phase23c_bridge **out);
int phase23c_destroy(phase23c_bridge *bridge);
int phase23c_import(phase23c_bridge *bridge, const char *image_name,
                    const uint8_t *object_file, size_t length,
                    const phase22d_class_spec *trusted, size_t count,
                    phase23c_image *image, char *error, size_t error_cap);
int phase23c_category(phase23c_bridge *bridge, phase23c_image image,
                      const phase22d_category *category);
int phase23c_allocate(phase23c_bridge *bridge, phase23c_image image,
                      const char *class_name, void **out);
int phase23c_retain(phase23c_bridge *bridge, const void *object);
int phase23c_release(phase23c_bridge *bridge, const void *object);
int phase23c_selector(phase23c_bridge *bridge, const char *name,
                      phase22d_sel *out);
int phase23c_send_words(phase23c_bridge *bridge, void *object,
                        phase22d_sel selector, const char *lexical_super,
                        const uintptr_t *args, size_t count, uintptr_t *out);
int phase23c_send_double1(phase23c_bridge *bridge, void *object,
                          phase22d_sel selector, const char *lexical_super,
                          double arg, double *out);
int phase23c_weak_store(phase23c_bridge *bridge, void **slot, void *object);
int phase23c_weak_load_retained(phase23c_bridge *bridge, void **slot,
                                void **out);
int phase23c_weak_unregister(phase23c_bridge *bridge, void **slot);
int phase23c_statistics(phase23c_bridge *bridge, phase23c_stats *out);
int phase23c_validate(phase23c_bridge *bridge);
int phase23c_unload(phase23c_bridge *bridge, phase23c_image image);
#endif
