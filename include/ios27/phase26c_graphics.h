#ifndef IOS27_PHASE26C_GRAPHICS_H
#define IOS27_PHASE26C_GRAPHICS_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct p26_device p26_device;
typedef uint64_t p26_handle;
typedef enum {
    P26_OK = 0, P26_INVALID, P26_UNSUPPORTED, P26_BOUNDS,
    P26_EXHAUSTED, P26_STALE, P26_BUSY, P26_PENDING, P26_DEVICE_LOST
} p26_status;
typedef enum { P26_RGBA8 = 1, P26_BGRA8 = 2, P26_R8 = 3 } p26_format;
typedef enum { P26_BUFFER = 1, P26_TEXTURE = 2, P26_SURFACE = 3,
               P26_QUEUE = 4, P26_COMMAND = 5, P26_FRAME = 6 } p26_kind;
typedef struct {
    uint32_t width, height, stride;
    p26_format format;
} p26_image_desc;
typedef struct {
    p26_handle handle;
    uint64_t token;
    void *bytes;
    size_t length;
} p26_mapping;
typedef struct {
    p26_handle queue;
    uint64_t sequence;
} p26_fence;

/* One device per isolated guest. Handles include namespace, generation and slot.
 * Every call except device_create/destroy synchronizes on the device lock.
 * Callers must finish all concurrent calls before device_destroy. */
p26_device *p26_device_create(size_t budget_bytes);
p26_status p26_device_shutdown(p26_device *device);
/* Simulated or reported device loss is terminal; release handles to retire. */
p26_status p26_device_lose(p26_device *device);
void p26_device_destroy(p26_device *device);
size_t p26_device_used(p26_device *device);
p26_status p26_buffer_create(p26_device *device, size_t bytes,
                             size_t alignment, p26_handle *out);
p26_status p26_image_create(p26_device *device, p26_kind kind,
                            p26_image_desc desc, p26_handle *out);
p26_status p26_image_info(p26_device *device, p26_handle image,
                          p26_image_desc *desc);
p26_status p26_queue_create(p26_device *device, p26_handle *out);
p26_status p26_command_create(p26_device *device, p26_handle *out);
p26_status p26_retain(p26_device *device, p26_handle handle);
p26_status p26_release(p26_device *device, p26_handle handle);
p26_status p26_resource_lock(p26_device *device, p26_handle handle,
                             p26_mapping *out);
p26_status p26_resource_unlock(p26_device *device, p26_mapping *mapping);
p26_status p26_write(p26_device *device, p26_handle handle,
                     size_t offset, const void *bytes, size_t length);
p26_status p26_read(p26_device *device, p26_handle handle,
                    size_t offset, void *bytes, size_t length);
/* Queue retains all referenced objects until command execution or destruction. */
p26_status p26_command_copy(p26_device *device, p26_handle command,
                            p26_handle source, p26_handle destination,
                            size_t source_offset, size_t destination_offset,
                            size_t bytes);
p26_status p26_command_blit(p26_device *device, p26_handle command,
                            p26_handle source_image, p26_handle destination_image);
p26_status p26_command_upload(p26_device *device, p26_handle command,
                              p26_handle source_buffer, p26_handle destination_image,
                              size_t source_offset, size_t source_stride);
p26_status p26_submit(p26_device *device, p26_handle queue,
                      p26_handle command, p26_fence *out);
p26_status p26_fence_poll(p26_device *device, p26_fence fence);
/* A presentation copies pixels to an independent immutable frame handle.
 * Frame remains valid after the input image is released. */
p26_status p26_present(p26_device *device, p26_handle image,
                       p26_handle *frame);
p26_status p26_frame_info(p26_device *device, p26_handle frame,
                          p26_image_desc *desc, uint64_t *hash);
#ifdef __cplusplus
}
#endif
#endif
