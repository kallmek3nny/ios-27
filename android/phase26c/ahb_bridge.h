#ifndef IOS27_PHASE26C_AHB_BRIDGE_H
#define IOS27_PHASE26C_AHB_BRIDGE_H
#include "ios27/phase26c_graphics.h"
#if defined(__ANDROID__)
#include <android/hardware_buffer.h>
#else
typedef struct AHardwareBuffer AHardwareBuffer;
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* Explicit CPU-staging copies, NOT zero-copy IOSurface / Metal / Vulkan interop.
 * The caller owns one reference to *out; release with AHardwareBuffer_release.
 * Only RGBA8 resources are accepted. No fence FD is imported/exported. */
p26_status p26_ahb_export_staged(p26_device *d, p26_handle image,
                                 AHardwareBuffer **out);
p26_status p26_ahb_import_staged(p26_device *d, AHardwareBuffer *buffer,
                                 p26_handle *out_surface);
#ifdef __cplusplus
}
#endif
#endif
