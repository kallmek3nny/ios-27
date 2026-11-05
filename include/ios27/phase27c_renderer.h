#ifndef IOS27_PHASE27C_RENDERER_H
#define IOS27_PHASE27C_RENDERER_H
#include "ios27/phase26c_graphics.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define P27C_FRAME_ABI 1U
#define P27C_MAX_INFLIGHT 128U

typedef struct p27c_renderer p27c_renderer;

/* Versioned, copyable presentation receipt. The renderer owns the device;
 * the consumer owns ONE frame reference until p27c_frame_release succeeds.
 * A completed CPU fence is not a Vulkan/Android sync-fence FD.
 * Visible pixels have tightly packed RGBA8/BGRA8 channels in each row;
 * row pitch is 16-byte aligned and padding is excluded from the hash.
 * Read or copy the frame before release. Renderer destruction returns BUSY
 * while any receipt is outstanding. Cross-thread calls are synchronized,
 * but users must not race renderer_destroy with entry into another API call. */
typedef struct {
    uint32_t abi_version;
    p26_image_desc image;
    p26_handle frame;
    p26_fence ready;
    uint64_t visible_hash;
} p27c_frame;

/* Supports RGBA8/BGRA8 output, CPU-only. No genuine Apple Metal/IOSurface.
 * budget_bytes accounts for staging, intermediate, surface and live frames. */
p27c_renderer *p27c_renderer_create(uint32_t width, uint32_t height,
                                    p26_format output_format, size_t budget_bytes);
p26_status p27c_renderer_destroy(p27c_renderer **renderer);
size_t p27c_renderer_used(p27c_renderer *renderer);

/* Begin copies a straight-alpha RGBA8 clear color into a mapped staging
 * buffer. All draws blend source-over in fixed-point with round-to-nearest.
 * Coordinates and uploads are checked before any pixels are changed.
 * Call cancel to discard an in-progress frame. */
p26_status p27c_begin(p27c_renderer *renderer, const uint8_t clear_rgba[4]);
p26_status p27c_rect(p27c_renderer *renderer, uint32_t x, uint32_t y,
                     uint32_t width, uint32_t height, const uint8_t rgba[4]);
p26_status p27c_upload_rgba(p27c_renderer *renderer, uint32_t x, uint32_t y,
                            uint32_t width, uint32_t height,
                            const uint8_t *pixels, size_t pixels_length,
                            size_t source_stride);
p26_status p27c_cancel(p27c_renderer *renderer);

/* Submits upload and RGBA/BGRA blit through phase26c, polls its synchronous
 * completion fence, then snapshots an immutable frame. */
p26_status p27c_end(p27c_renderer *renderer, p27c_frame *out);

/* An Android host can copy rows into an RGBA8888 AHardwareBuffer mapping,
 * respecting destination_stride. No host pointers, FD or GPU memory escape.
 * The caller supplies storage for (height-1)*stride + width*4 bytes.
 * The frame stays readable after any later p27c_begin/p27c_end call. */
p26_status p27c_frame_read(p27c_renderer *renderer, const p27c_frame *frame,
                           uint8_t *destination, size_t destination_length,
                           size_t destination_stride);
p26_status p27c_frame_release(p27c_renderer *renderer, p27c_frame *frame);
#ifdef __cplusplus
}
#endif
#endif
