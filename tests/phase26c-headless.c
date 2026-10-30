#include "ios27/phase26c_graphics.h"
#include "../android/phase26c/ahb_bridge.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed %s:%d: %s\n", __FILE__, __LINE__, #x); abort(); } } while (0)
#define OK(x) CHECK((x) == P26_OK)

static uint64_t reference_hash(uint32_t width, uint32_t height, p26_format fmt,
                               const unsigned char *data, size_t stride) {
    uint64_t hash = UINT64_C(14695981039346656037);
    uint32_t props[] = {width, height, (uint32_t)fmt};
    for (size_t i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 4; ++j)
            hash = (hash ^ ((props[i] >> (j * 8U)) & 255U)) * UINT64_C(1099511628211);
    size_t pixel = fmt == P26_R8 ? 1U : 4U;
    for (size_t y = 0; y < height; ++y)
        for (size_t x = 0; x < width * pixel; ++x)
            hash = (hash ^ data[y * stride + x]) * UINT64_C(1099511628211);
    return hash;
}
typedef struct { p26_device *device; p26_handle resource; } worker_args;
static void *worker(void *arg) {
    worker_args *a = (worker_args *)arg;
    for (unsigned i = 0; i < 25000U; ++i) {
        OK(p26_retain(a->device, a->resource));
        OK(p26_release(a->device, a->resource));
    }
    return NULL;
}
int main(void) {
    p26_device *d = p26_device_create(1024U * 1024U);
    p26_device *other = p26_device_create(1024U);
    CHECK(d && other);
    p26_handle src = 0, dst = 0, texture = 0, surface = 0, queue = 0, cmd = 0;
    CHECK(p26_buffer_create(d, 1, 3, &src) == P26_INVALID);
    CHECK(p26_buffer_create(d, SIZE_MAX, 64, &src) == P26_INVALID);
    OK(p26_buffer_create(d, 1024, 64, &src));
    CHECK(p26_retain(other, src) == P26_STALE);
    OK(p26_buffer_create(d, 1024, 64, &dst));
    p26_image_desc desc = {16U, 8U, 64U, P26_RGBA8};
    CHECK(p26_image_create(d, P26_TEXTURE, (p26_image_desc){0U, 8U, 64U, P26_RGBA8}, &texture) == P26_INVALID);
    CHECK(p26_image_create(d, P26_TEXTURE, (p26_image_desc){16U, 8U, 63U, P26_RGBA8}, &texture) == P26_INVALID);
    CHECK(p26_image_create(d, P26_TEXTURE, (p26_image_desc){16U, 8U, 64U, (p26_format)99}, &texture) == P26_INVALID);
    CHECK(p26_image_create(d, P26_TEXTURE, (p26_image_desc){UINT32_MAX, UINT32_MAX, UINT32_MAX, P26_RGBA8}, &texture) == P26_INVALID);
    OK(p26_image_create(d, P26_TEXTURE, desc, &texture));
    desc.format = P26_BGRA8;
    OK(p26_image_create(d, P26_SURFACE, desc, &surface));
    OK(p26_queue_create(d, &queue));
    OK(p26_command_create(d, &cmd));
    unsigned char pixels[512], expected[512], observed[512];
    for (size_t i = 0; i < sizeof(pixels); ++i) pixels[i] = (unsigned char)(i * 13U + 7U);
    for (size_t i = 0; i < sizeof(expected); i += 4) {
        expected[i] = pixels[i + 2U];
        expected[i + 1U] = pixels[i + 1U];
        expected[i + 2U] = pixels[i];
        expected[i + 3U] = pixels[i + 3U];
    }
    OK(p26_write(d, src, 0, pixels, sizeof(pixels)));
    CHECK(p26_write(d, src, SIZE_MAX, pixels, 1U) == P26_BOUNDS);
    OK(p26_command_upload(d, cmd, src, texture, 0, 64));
    OK(p26_command_blit(d, cmd, texture, surface));
    p26_fence fence;
    OK(p26_submit(d, queue, cmd, &fence));
    OK(p26_fence_poll(d, fence));
    CHECK(p26_fence_poll(d, (p26_fence){queue, fence.sequence + 1U}) == P26_PENDING);
    OK(p26_read(d, surface, 0, observed, sizeof(observed)));
    CHECK(memcmp(expected, observed, sizeof(observed)) == 0);
    p26_handle frame;
    OK(p26_present(d, surface, &frame));
    uint64_t hash = 0;
    p26_image_desc frame_desc;
    OK(p26_frame_info(d, frame, &frame_desc, &hash));
    CHECK(frame_desc.format == P26_BGRA8);
    CHECK(hash == reference_hash(16U, 8U, P26_BGRA8, expected, 64U));
    /* Presentation storage is independent from the input surface. */
    OK(p26_release(d, surface));
    CHECK(p26_release(d, surface) == P26_STALE);
    OK(p26_read(d, frame, 0, observed, sizeof(observed)));
    CHECK(memcmp(observed, expected, sizeof(observed)) == 0);
    CHECK(p26_write(d, frame, 0, pixels, 1U) == P26_INVALID);
    OK(p26_release(d, frame));
    /* Fail-closed submission with a valid first command must not partially write. */
    OK(p26_command_copy(d, cmd, src, dst, 0, 0, 16));
    OK(p26_command_copy(d, cmd, src, dst, 1020, 0, 16));
    CHECK(p26_submit(d, queue, cmd, &fence) == P26_BOUNDS);
    OK(p26_read(d, dst, 0, observed, 16));
    for (unsigned i = 0; i < 16U; ++i) CHECK(observed[i] == 0);
    OK(p26_release(d, cmd));
    OK(p26_command_create(d, &cmd));
    CHECK(p26_command_copy(d, cmd, cmd, dst, 0, 0, 1) == P26_INVALID);
    p26_mapping map;
    OK(p26_resource_lock(d, src, &map));
    CHECK(((uintptr_t)map.bytes % 64U) == 0);
    CHECK(p26_resource_lock(d, src, &((p26_mapping){0})) == P26_BUSY);
    CHECK(p26_write(d, src, 0, pixels, 1U) == P26_BUSY);
    OK(p26_command_copy(d, cmd, src, dst, 0, 0, 512));
    CHECK(p26_submit(d, queue, cmd, &fence) == P26_BUSY);
    OK(p26_release(d, src)); /* map + command keep it alive */
    CHECK(p26_release(d, src) == P26_STALE); /* cannot steal internal pins */
    CHECK(p26_retain(d, src) == P26_STALE);
    CHECK(p26_read(d, src, 0, observed, 1) == P26_STALE);
    CHECK(p26_device_shutdown(d) == P26_BUSY);
    OK(p26_resource_unlock(d, &map));
    CHECK(p26_resource_unlock(d, &map) == P26_STALE);
    OK(p26_submit(d, queue, cmd, &fence));
    CHECK(p26_retain(d, src) == P26_STALE);
    OK(p26_read(d, dst, 0, observed, 512));
    CHECK(memcmp(observed, pixels, 512) == 0);
    OK(p26_release(d, texture));
    OK(p26_release(d, dst));
    OK(p26_release(d, cmd));
    OK(p26_release(d, queue));
    CHECK(p26_device_used(d) == 0U);
    /* A 10k-frame stream, stable golden hashes, and independent frame lifetimes. */
    p26_handle streamed;
    OK(p26_image_create(d, P26_SURFACE, (p26_image_desc){1, 1, 16, P26_RGBA8}, &streamed));
    const unsigned char sample[16] = {19, 39, 59, 255};
    OK(p26_write(d, streamed, 0, sample, sizeof(sample)));
    uint64_t golden = reference_hash(1, 1, P26_RGBA8, sample, 16);
    for (unsigned i = 0; i < 10000U; ++i) {
        OK(p26_present(d, streamed, &frame));
        OK(p26_frame_info(d, frame, &frame_desc, &hash));
        CHECK(hash == golden);
        OK(p26_release(d, frame));
    }
    OK(p26_release(d, streamed));
    /* 100k resource lifetimes and stale generation defenses. */
    p26_handle old = 0;
    for (unsigned i = 0; i < 100000U; ++i) {
        p26_handle current;
        OK(p26_buffer_create(d, 32, 16, &current));
        if (old) CHECK(p26_retain(d, old) == P26_STALE);
        old = current;
        OK(p26_release(d, current));
    }
    /* Four threads perform 100k additional ref/unref pairs under contention. */
    p26_handle shared;
    OK(p26_buffer_create(d, 64, 16, &shared));
    worker_args args = {d, shared};
    pthread_t threads[4];
    for (unsigned i = 0; i < 4U; ++i) CHECK(pthread_create(&threads[i], NULL, worker, &args) == 0);
    for (unsigned i = 0; i < 4U; ++i) CHECK(pthread_join(threads[i], NULL) == 0);
    OK(p26_release(d, shared));
    CHECK(p26_device_used(d) == 0U);
    OK(p26_device_shutdown(d));
    CHECK(p26_buffer_create(d, 16, 16, &shared) == P26_INVALID);
    p26_device_destroy(d);
    OK(p26_device_shutdown(other));
    p26_device_destroy(other);
    /* Invalid host AHB path rejects rather than faking an interop success. */
#if !defined(__ANDROID__)
    CHECK(p26_ahb_export_staged(NULL, 0, NULL) == P26_UNSUPPORTED);
    CHECK(p26_ahb_import_staged(NULL, NULL, NULL) == P26_UNSUPPORTED);
#endif
    p26_device *tiny = p26_device_create(64U);
    CHECK(tiny != NULL);
    CHECK(p26_image_create(tiny, P26_SURFACE,
          (p26_image_desc){8, 8, 32, P26_RGBA8}, &shared) == P26_EXHAUSTED);
    OK(p26_buffer_create(tiny, 64, 16, &shared));
    CHECK(p26_buffer_create(tiny, 1, 1, &frame) == P26_EXHAUSTED);
    OK(p26_device_lose(tiny));
    CHECK(p26_write(tiny, shared, 0, sample, 1) == P26_DEVICE_LOST);
    CHECK(p26_buffer_create(tiny, 1, 1, &frame) == P26_DEVICE_LOST);
    OK(p26_release(tiny, shared));
    OK(p26_device_shutdown(tiny));
    p26_device_destroy(tiny);
    puts("phase26c headless: 10k deterministic frames, 100k resource lifetimes, 100k threaded retain/release pairs PASS");
    return 0;
}