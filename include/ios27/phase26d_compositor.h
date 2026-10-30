#ifndef IOS27_PHASE26D_COMPOSITOR_H
#define IOS27_PHASE26D_COMPOSITOR_H
#include <stddef.h>
#include <stdint.h>

#define PHASE26D_MAX_LAYERS 256u
#define PHASE26D_MAX_TX_DEPTH 8u
#define PHASE26D_MAX_FRAME_DIM 512u
#define PHASE26D_MAX_IMAGE_DIM 256u
#define PHASE26D_MAX_IMAGE_BYTES (4u * 1024u * 1024u)
#define PHASE26D_IMAGE_BUDGET (16u * 1024u * 1024u)

typedef struct phase26d_context phase26d_context;
typedef struct { uint32_t slot, generation; } phase26d_id;
typedef struct { double a, b, c, d, tx, ty; } phase26d_affine;
typedef struct {
    int32_t x, y, width, height, z;
    phase26d_affine transform;
    uint8_t opacity, clips, corner_radius, blur_radius;
    uint8_t material, shadow_alpha, shadow_radius;
    int8_t shadow_x, shadow_y;
    uint8_t rgba[4]; /* straight RGBA for the flat fill */
} phase26d_layer_desc;
typedef struct {
    uint8_t *pixels; /* caller-owned premultiplied RGBA8 */
    uint32_t width, height, stride;
    size_t capacity; /* accessible bytes from pixels, including row gaps */
} phase26d_frame;
typedef struct {
    uint64_t commits, mutations, frames, image_bytes, live_images;
    uint32_t model_layers, presented_layers, transaction_depth;
} phase26d_stats;
typedef enum {
    PHASE26D_OK=0, PHASE26D_INVALID=-1, PHASE26D_LIMIT=-2,
    PHASE26D_STALE=-3, PHASE26D_STATE=-4, PHASE26D_MEMORY=-5
} phase26d_result;
typedef enum { PHASE26D_ANIM_OPACITY=1, PHASE26D_ANIM_X=2, PHASE26D_ANIM_Y=3 } phase26d_anim_kind;

/* All entrypoints synchronize internally. No borrowed layer/image pointers escape. */
phase26d_context *phase26d_new(void);
void phase26d_free(phase26d_context *ctx);
phase26d_layer_desc phase26d_default_desc(void);
phase26d_result phase26d_begin(phase26d_context *ctx);
phase26d_result phase26d_commit(phase26d_context *ctx);
phase26d_result phase26d_rollback(phase26d_context *ctx);
phase26d_result phase26d_create(phase26d_context *ctx, const phase26d_layer_desc *d, phase26d_id *out);
phase26d_result phase26d_destroy(phase26d_context *ctx, phase26d_id id);
phase26d_result phase26d_set(phase26d_context *ctx, phase26d_id id, const phase26d_layer_desc *d);
phase26d_result phase26d_parent(phase26d_context *ctx, phase26d_id child, phase26d_id parent);
phase26d_result phase26d_upload(phase26d_context *ctx, phase26d_id id, const uint8_t *pixels,
                                uint32_t width, uint32_t height, uint32_t stride, size_t source_bytes);
phase26d_result phase26d_animate(phase26d_context *ctx, phase26d_id id, phase26d_anim_kind kind,
                                 double from, double to, uint64_t start_ns, uint64_t duration_ns);
phase26d_result phase26d_cancel(phase26d_context *ctx, phase26d_id id, phase26d_anim_kind kind);
phase26d_result phase26d_step(phase26d_context *ctx, uint64_t timestamp_ns);
phase26d_result phase26d_get(phase26d_context *ctx, phase26d_id id, int presentation,
                             phase26d_layer_desc *out);
phase26d_result phase26d_render(phase26d_context *ctx, phase26d_frame frame);
phase26d_result phase26d_stats_read(phase26d_context *ctx, phase26d_stats *out);
#endif
