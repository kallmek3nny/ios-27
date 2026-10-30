#include "ahb_bridge.h"
#if defined(__ANDROID__) && __ANDROID_API__ >= 26
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <android/hardware_buffer.h>

static int good_dimensions(uint32_t w, uint32_t h) {
    return w > 0 && h > 0 && w <= 8192U && h <= 8192U;
}
static uint32_t row_aligned(uint32_t width) {
    return (width * 4U + 15U) & ~15U;
}
p26_status p26_ahb_export_staged(p26_device *d, p26_handle image,
                                 AHardwareBuffer **out) {
    if (!d || !out) return P26_INVALID;
    p26_image_desc src;
    p26_status result = p26_image_info(d, image, &src);
    if (result != P26_OK) return result;
    if (src.format != P26_RGBA8 || !good_dimensions(src.width, src.height))
        return P26_UNSUPPORTED;
    AHardwareBuffer_Desc request = {0};
    request.width = src.width;
    request.height = src.height;
    request.layers = 1U;
    request.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    request.usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
                    AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
    AHardwareBuffer *buffer = NULL;
    if (AHardwareBuffer_allocate(&request, &buffer) != 0 || !buffer)
        return P26_UNSUPPORTED;
    AHardwareBuffer_Desc actual;
    AHardwareBuffer_describe(buffer, &actual);
    if (actual.width != src.width || actual.height != src.height ||
        actual.format != request.format || actual.stride < src.width ||
        actual.stride > 262144U) {
        AHardwareBuffer_release(buffer);
        return P26_UNSUPPORTED;
    }
    void *pixels = NULL;
    if (AHardwareBuffer_lock(buffer, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,
                             -1, NULL, &pixels) != 0 || !pixels) {
        AHardwareBuffer_release(buffer);
        return P26_BUSY;
    }
    result = P26_OK;
    for (uint32_t y = 0; y < src.height; ++y) {
        unsigned char *row = (unsigned char *)pixels + (size_t)y * actual.stride * 4U;
        result = p26_read(d, image, (size_t)y * src.stride, row,
                          (size_t)src.width * 4U);
        if (result != P26_OK) break;
    }
    int unlocked = AHardwareBuffer_unlock(buffer, NULL);
    if (result != P26_OK || unlocked != 0) {
        AHardwareBuffer_release(buffer);
        return result != P26_OK ? result : P26_BUSY;
    }
    *out = buffer;
    return P26_OK;
}
p26_status p26_ahb_import_staged(p26_device *d, AHardwareBuffer *buffer,
                                 p26_handle *out_surface) {
    if (!d || !buffer || !out_surface) return P26_INVALID;
    AHardwareBuffer_acquire(buffer);
    AHardwareBuffer_Desc actual;
    AHardwareBuffer_describe(buffer, &actual);
    if (!good_dimensions(actual.width, actual.height) ||
        actual.stride < actual.width || actual.stride > 262144U || actual.format != AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM ||
        actual.layers != 1U) {
        AHardwareBuffer_release(buffer);
        return P26_UNSUPPORTED;
    }
    p26_image_desc dst = {actual.width, actual.height,
                          row_aligned(actual.width), P26_RGBA8};
    p26_handle surface = 0;
    p26_status result = p26_image_create(d, P26_SURFACE, dst, &surface);
    if (result != P26_OK) { AHardwareBuffer_release(buffer); return result; }
    void *pixels = NULL;
    if (AHardwareBuffer_lock(buffer, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
                             -1, NULL, &pixels) != 0 || !pixels) {
        p26_release(d, surface);
        AHardwareBuffer_release(buffer);
        return P26_BUSY;
    }
    for (uint32_t y = 0; y < dst.height; ++y) {
        const unsigned char *row = (const unsigned char *)pixels +
                                   (size_t)y * actual.stride * 4U;
        result = p26_write(d, surface, (size_t)y * dst.stride, row,
                           (size_t)dst.width * 4U);
        if (result != P26_OK) break;
    }
    int unlocked = AHardwareBuffer_unlock(buffer, NULL);
    AHardwareBuffer_release(buffer);
    if (result != P26_OK || unlocked != 0) {
        p26_release(d, surface);
        return result != P26_OK ? result : P26_BUSY;
    }
    *out_surface = surface;
    return P26_OK;
}
#else
p26_status p26_ahb_export_staged(p26_device *d, p26_handle image,
                                 AHardwareBuffer **out) {
    (void)d; (void)image; (void)out;
    return P26_UNSUPPORTED;
}
p26_status p26_ahb_import_staged(p26_device *d, AHardwareBuffer *buffer,
                                 p26_handle *out_surface) {
    (void)d; (void)buffer; (void)out_surface;
    return P26_UNSUPPORTED;
}
#endif
