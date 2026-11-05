#include "ios27/phase27c_renderer.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "phase27c failure %s:%d: %s\n", __FILE__, __LINE__, #test); \
    abort(); } } while (0)
#define OK(test) CHECK((test) == P26_OK)

static const uint8_t expected_rgba[32] = {
    10,20,30,255, 105,10,65,255, 105,10,65,255, 10,20,30,255,
    10,20,30,255, 105,10,65,255, 52,105,32,255, 10,20,30,255
};
static const uint8_t expected_bgra[32] = {
    30,20,10,255, 65,10,105,255, 65,10,105,255, 30,20,10,255,
    30,20,10,255, 65,10,105,255, 32,105,52,255, 30,20,10,255
};
static const uint8_t clear_color[4] = {10,20,30,255};
static const uint8_t overlay_color[4] = {200,0,100,128};
static const uint8_t upload_pixel[4] = {0,200,0,128};

static uint64_t independent_hash(const uint8_t *pixels, p26_format fmt) {
    uint64_t h = UINT64_C(14695981039346656037);
    const uint32_t properties[3] = {4U,2U,(uint32_t)fmt};
    for (size_t p = 0; p < 3U; ++p)
        for (unsigned byte = 0; byte < 4U; ++byte)
            h = (h ^ ((properties[p] >> (8U * byte)) & 255U)) *
                UINT64_C(1099511628211);
    for (size_t i = 0; i < 32U; ++i)
        h = (h ^ pixels[i]) * UINT64_C(1099511628211);
    return h;
}
static void draw_fixture(p27c_renderer *r) {
    OK(p27c_begin(r, clear_color));
    CHECK(p27c_begin(r, clear_color) == P26_BUSY);
    CHECK(p27c_rect(r, 3U, 0U, 2U, 1U, overlay_color) == P26_BOUNDS);
    CHECK(p27c_rect(r, UINT32_MAX, 0U, 1U, 1U, overlay_color) == P26_BOUNDS);
    CHECK(p27c_upload_rgba(r, 2U, 1U, 1U, 1U,
                           upload_pixel, 3U, 4U) == P26_BOUNDS);
    OK(p27c_rect(r, 1U, 0U, 2U, 2U, overlay_color));
    OK(p27c_upload_rgba(r, 2U, 1U, 1U, 1U, upload_pixel, 4U, 4U));
}
typedef struct {
    p27c_renderer *r;
    const p27c_frame *frame;
    const uint8_t *expected;
} reader_args;
static void *parallel_read(void *arg) {
    reader_args *a = (reader_args *)arg;
    uint8_t bytes[64];
    for (unsigned i = 0U; i < 500U; ++i) {
        memset(bytes, 0xAB, sizeof(bytes));
        OK(p27c_frame_read(a->r, a->frame, bytes, sizeof(bytes), 32U));
        CHECK(memcmp(bytes, a->expected, 16U) == 0);
        CHECK(memcmp(bytes + 32U, a->expected + 16U, 16U) == 0);
        CHECK(bytes[16] == 0xAB && bytes[48] == 0xAB);
    }
    return NULL;
}
static void fixture(p26_format fmt, const uint8_t *expected, uint64_t golden) {
    p27c_renderer *r = p27c_renderer_create(4U, 2U, fmt, 8192U);
    CHECK(r != NULL);
    CHECK(p27c_renderer_used(r) == 96U);
    draw_fixture(r);
    p27c_frame frame = {0};
    OK(p27c_end(r, &frame));
    CHECK(frame.abi_version == P27C_FRAME_ABI);
    CHECK(frame.image.format == fmt && frame.image.width == 4U);
    CHECK(frame.image.height == 2U && frame.image.stride == 16U);
    CHECK(frame.ready.sequence == 1U && frame.ready.queue != 0U);
    CHECK(frame.visible_hash == golden);
    CHECK(frame.visible_hash == independent_hash(expected, fmt));
    CHECK(p27c_renderer_used(r) == 128U);
    uint8_t pixels[32] = {0};
    CHECK(p27c_frame_read(r, &frame, pixels, sizeof(pixels)-1U, 16U) == P26_BOUNDS);
    OK(p27c_frame_read(r, &frame, pixels, sizeof(pixels), 16U));
    CHECK(memcmp(pixels, expected, sizeof(pixels)) == 0);
    CHECK(p27c_renderer_destroy(&r) == P26_BUSY && r != NULL);

    /* Immutable snapshot must survive subsequent submissions. */
    const uint8_t black[4] = {0,0,0,255};
    OK(p27c_begin(r, black));
    p27c_frame newer = {0};
    OK(p27c_end(r, &newer));
    CHECK(newer.ready.sequence == 2U);
    OK(p27c_frame_read(r, &frame, pixels, sizeof(pixels), 16U));
    CHECK(memcmp(pixels, expected, sizeof(pixels)) == 0);

    reader_args args = {r, &frame, expected};
    pthread_t threads[4];
    for (size_t i = 0; i < 4U; ++i)
        CHECK(pthread_create(&threads[i], NULL, parallel_read, &args) == 0);
    for (size_t i = 0; i < 4U; ++i) CHECK(pthread_join(threads[i], NULL) == 0);

    p27c_frame stale = frame;
    OK(p27c_frame_release(r, &frame));
    CHECK(frame.frame == 0U);
    CHECK(p27c_frame_release(r, &stale) == P26_STALE);
    CHECK(p27c_frame_read(r, &stale, pixels, sizeof(pixels), 16U) == P26_STALE);
    OK(p27c_frame_release(r, &newer));
    CHECK(p27c_renderer_used(r) == 96U);

    /* 10,000 complete native command -> snapshot -> pixel-read cycles. */
    for (unsigned n = 0; n < 10000U; ++n) {
        draw_fixture(r);
        OK(p27c_end(r, &frame));
        CHECK(frame.visible_hash == golden);
        OK(p27c_frame_read(r, &frame, pixels, sizeof(pixels), 16U));
        CHECK(memcmp(pixels, expected, sizeof(pixels)) == 0);
        OK(p27c_frame_release(r, &frame));
    }

    /* Bounded frame backlog plus graceful teardown without dropping readers. */
    p27c_frame tickets[P27C_MAX_INFLIGHT];
    for (size_t i = 0; i < P27C_MAX_INFLIGHT; ++i) {
        OK(p27c_begin(r, black));
        OK(p27c_end(r, &tickets[i]));
    }
    OK(p27c_begin(r, black));
    CHECK(p27c_end(r, &frame) == P26_EXHAUSTED);
    OK(p27c_cancel(r));
    CHECK(p27c_renderer_destroy(&r) == P26_BUSY);
    for (size_t i = 0; i < P27C_MAX_INFLIGHT; ++i)
        OK(p27c_frame_release(r, &tickets[i]));
    CHECK(p27c_renderer_used(r) == 96U);
    OK(p27c_renderer_destroy(&r));
    CHECK(r == NULL);
}
int main(void) {
    CHECK(p27c_renderer_create(4U, 2U, P26_R8, 8192U) == NULL);
    CHECK(p27c_renderer_create(UINT32_MAX, 2U, P26_RGBA8, 8192U) == NULL);
    CHECK(p27c_renderer_create(4U, 2U, P26_RGBA8, 95U) == NULL);
    CHECK(independent_hash(expected_rgba, P26_RGBA8) == UINT64_C(0x06c3261fef3963a7));
    CHECK(independent_hash(expected_bgra, P26_BGRA8) == UINT64_C(0x52f13f4c4681c790));
    fixture(P26_RGBA8, expected_rgba, UINT64_C(0x06c3261fef3963a7));
    fixture(P26_BGRA8, expected_bgra, UINT64_C(0x52f13f4c4681c790));

    /* Reject a frame snapshot when the device quota only fits base resources. */
    p27c_renderer *r = p27c_renderer_create(4U, 2U, P26_RGBA8, 96U);
    CHECK(r != NULL);
    OK(p27c_begin(r, clear_color));
    p27c_frame receipt = {0};
    CHECK(p27c_end(r, &receipt) == P26_EXHAUSTED);
    CHECK(receipt.frame == 0U);
    CHECK(p27c_renderer_used(r) == 96U);
    OK(p27c_renderer_destroy(&r));

    puts("phase27c PASS: rgba hash 06c3261fef3963a7; bgra hash 52f13f4c4681c790; 20k rendered frames, independent snapshots, 4 concurrent readers");
    return 0;
}
