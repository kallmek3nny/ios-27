#include "ios27/phase27c_renderer.h"
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct p27c_renderer {
    pthread_mutex_t mutex;
    p26_device *device;
    p26_image_desc desc;
    p26_handle staging, texture, surface, queue;
    p26_mapping mapped;
    p26_handle frames[P27C_MAX_INFLIGHT];
    size_t inflight;
    int drawing;
};

static int dims_ok(uint32_t w, uint32_t h, size_t *pitch, size_t *bytes) {
    if (!w || !h || w > 8192U || h > 8192U) return 0;
    size_t visible = (size_t)w * 4U;
    size_t row = (visible + 15U) & ~(size_t)15U;
    if (row > UINT32_MAX || h > SIZE_MAX / row) return 0;
    *pitch = row;
    *bytes = row * h;
    return 1;
}
static int bounds_ok(const p26_image_desc *d, uint32_t x, uint32_t y,
                     uint32_t w, uint32_t h) {
    return w && h && x < d->width && y < d->height &&
           w <= d->width - x && h <= d->height - y;
}
static void drop(p27c_renderer *r) {
    if (!r || !r->device) return;
    if (r->mapped.token) (void)p26_resource_unlock(r->device, &r->mapped);
    if (r->queue) (void)p26_release(r->device, r->queue);
    if (r->surface) (void)p26_release(r->device, r->surface);
    if (r->texture) (void)p26_release(r->device, r->texture);
    if (r->staging) (void)p26_release(r->device, r->staging);
    (void)p26_device_shutdown(r->device);
    p26_device_destroy(r->device);
    r->device = NULL;
}
p27c_renderer *p27c_renderer_create(uint32_t width, uint32_t height,
                                    p26_format format, size_t budget_bytes) {
    size_t pitch = 0, bytes = 0;
    if ((format != P26_RGBA8 && format != P26_BGRA8) ||
        !dims_ok(width, height, &pitch, &bytes) ||
        !budget_bytes || bytes > budget_bytes / 3U) return NULL;
    p27c_renderer *r = (p27c_renderer *)calloc(1U, sizeof(*r));
    if (!r) return NULL;
    if (pthread_mutex_init(&r->mutex, NULL) != 0) { free(r); return NULL; }
    r->device = p26_device_create(budget_bytes);
    if (!r->device) goto fail;
    r->desc = (p26_image_desc){width, height, (uint32_t)pitch, format};
    p26_image_desc intermediate = r->desc;
    intermediate.format = P26_RGBA8;
    if (p26_buffer_create(r->device, bytes, 64U, &r->staging) != P26_OK ||
        p26_image_create(r->device, P26_TEXTURE, intermediate, &r->texture) != P26_OK ||
        p26_image_create(r->device, P26_SURFACE, r->desc, &r->surface) != P26_OK ||
        p26_queue_create(r->device, &r->queue) != P26_OK) goto fail;
    return r;
fail:
    drop(r);
    pthread_mutex_destroy(&r->mutex);
    free(r);
    return NULL;
}
size_t p27c_renderer_used(p27c_renderer *r) {
    if (!r) return 0U;
    pthread_mutex_lock(&r->mutex);
    size_t used = p26_device_used(r->device);
    pthread_mutex_unlock(&r->mutex);
    return used;
}
p26_status p27c_renderer_destroy(p27c_renderer **pr) {
    if (!pr || !*pr) return P26_INVALID;
    p27c_renderer *r = *pr;
    pthread_mutex_lock(&r->mutex);
    if (r->inflight || r->drawing) {
        pthread_mutex_unlock(&r->mutex);
        return P26_BUSY;
    }
    drop(r);
    pthread_mutex_unlock(&r->mutex);
    pthread_mutex_destroy(&r->mutex);
    *pr = NULL;
    free(r);
    return P26_OK;
}
static uint8_t alpha_over(uint8_t dc, uint8_t da, uint8_t sc, uint8_t sa) {
    uint32_t denom = (uint32_t)sa * 255U + (uint32_t)da * (255U - sa);
    if (!denom) return 0U;
    uint32_t numer = (uint32_t)sc * sa * 255U +
                     (uint32_t)dc * da * (255U - sa);
    return (uint8_t)((numer + denom / 2U) / denom);
}
static void blend(uint8_t *dst, const uint8_t *src) {
    uint8_t sa = src[3], da = dst[3];
    uint32_t alpha = (uint32_t)sa * 255U + (uint32_t)da * (255U - sa);
    uint8_t out[4];
    for (unsigned i = 0U; i < 3U; ++i)
        out[i] = alpha_over(dst[i], da, src[i], sa);
    out[3] = (uint8_t)((alpha + 127U) / 255U);
    memcpy(dst, out, sizeof(out));
}
p26_status p27c_begin(p27c_renderer *r, const uint8_t color[4]) {
    if (!r || !color) return P26_INVALID;
    pthread_mutex_lock(&r->mutex);
    p26_status st = P26_BUSY;
    if (!r->drawing) {
        st = p26_resource_lock(r->device, r->staging, &r->mapped);
        if (st == P26_OK) {
            unsigned char *data = (unsigned char *)r->mapped.bytes;
            memset(data, 0, r->mapped.length);
            for (uint32_t y = 0; y < r->desc.height; ++y) {
                uint8_t *row = data + (size_t)y * r->desc.stride;
                for (uint32_t x = 0; x < r->desc.width; ++x)
                    memcpy(row + (size_t)x * 4U, color, 4U);
            }
            r->drawing = 1;
        }
    }
    pthread_mutex_unlock(&r->mutex);
    return st;
}
p26_status p27c_rect(p27c_renderer *r, uint32_t x, uint32_t y,
                     uint32_t width, uint32_t height, const uint8_t rgba[4]) {
    if (!r || !rgba) return P26_INVALID;
    pthread_mutex_lock(&r->mutex);
    p26_status st = !r->drawing ? P26_INVALID : P26_BOUNDS;
    if (r->drawing && bounds_ok(&r->desc, x, y, width, height)) {
        uint8_t *data = (uint8_t *)r->mapped.bytes;
        for (uint32_t row = 0; row < height; ++row)
            for (uint32_t col = 0; col < width; ++col)
                blend(data + (size_t)(y + row) * r->desc.stride +
                      (size_t)(x + col) * 4U, rgba);
        st = P26_OK;
    }
    pthread_mutex_unlock(&r->mutex);
    return st;
}
p26_status p27c_upload_rgba(p27c_renderer *r, uint32_t x, uint32_t y,
                            uint32_t width, uint32_t height,
                            const uint8_t *pixels, size_t length, size_t source_stride) {
    if (!r || !pixels) return P26_INVALID;
    pthread_mutex_lock(&r->mutex);
    p26_status st = !r->drawing ? P26_INVALID : P26_BOUNDS;
    if (r->drawing && bounds_ok(&r->desc, x, y, width, height) &&
        source_stride >= (size_t)width * 4U &&
        (size_t)(height - 1U) <= SIZE_MAX / source_stride) {
        size_t required = (size_t)(height - 1U) * source_stride +
                          (size_t)width * 4U;
        if (required <= length) {
            uint8_t *data = (uint8_t *)r->mapped.bytes;
            for (uint32_t row = 0; row < height; ++row)
                for (uint32_t col = 0; col < width; ++col)
                    blend(data + (size_t)(y + row) * r->desc.stride +
                          (size_t)(x + col) * 4U,
                          pixels + (size_t)row * source_stride +
                          (size_t)col * 4U);
            st = P26_OK;
        }
    }
    pthread_mutex_unlock(&r->mutex);
    return st;
}
p26_status p27c_cancel(p27c_renderer *r) {
    if (!r) return P26_INVALID;
    pthread_mutex_lock(&r->mutex);
    p26_status st = r->drawing ? p26_resource_unlock(r->device, &r->mapped) : P26_INVALID;
    if (st == P26_OK) r->drawing = 0;
    pthread_mutex_unlock(&r->mutex);
    return st;
}
p26_status p27c_end(p27c_renderer *r, p27c_frame *out) {
    if (!r || !out) return P26_INVALID;
    pthread_mutex_lock(&r->mutex);
    if (!r->drawing) { pthread_mutex_unlock(&r->mutex); return P26_INVALID; }
    if (r->inflight == P27C_MAX_INFLIGHT) {
        pthread_mutex_unlock(&r->mutex);
        return P26_EXHAUSTED;
    }
    p26_status st = p26_resource_unlock(r->device, &r->mapped);
    if (st != P26_OK) { pthread_mutex_unlock(&r->mutex); return st; }
    r->drawing = 0;
    p26_handle command = 0, snapshot = 0;
    p26_fence ready = {0};
    st = p26_command_create(r->device, &command);
    if (st == P26_OK)
        st = p26_command_upload(r->device, command, r->staging, r->texture,
                                0U, r->desc.stride);
    if (st == P26_OK)
        st = p26_command_blit(r->device, command, r->texture, r->surface);
    if (st == P26_OK) st = p26_submit(r->device, r->queue, command, &ready);
    if (command) (void)p26_release(r->device, command);
    if (st == P26_OK) st = p26_fence_poll(r->device, ready);
    if (st == P26_OK) st = p26_present(r->device, r->surface, &snapshot);
    p26_image_desc info = {0};
    uint64_t hash = 0U;
    if (st == P26_OK) st = p26_frame_info(r->device, snapshot, &info, &hash);
    if (st == P26_OK) {
        *out = (p27c_frame){P27C_FRAME_ABI, info, snapshot, ready, hash};
        r->frames[r->inflight++] = snapshot;
    } else if (snapshot) {
        (void)p26_release(r->device, snapshot);
    }
    pthread_mutex_unlock(&r->mutex);
    return st;
}
static int ticket_ok(p27c_renderer *r, const p27c_frame *f, size_t *index) {
    if (f->abi_version != P27C_FRAME_ABI ||
        f->image.width != r->desc.width || f->image.height != r->desc.height ||
        f->image.stride != r->desc.stride ||
        f->image.format != r->desc.format) return 0;
    for (size_t i = 0; i < r->inflight; ++i) {
        if (r->frames[i] == f->frame) { *index = i; return 1; }
    }
    return 0;
}
p26_status p27c_frame_read(p27c_renderer *r, const p27c_frame *f,
                           uint8_t *dst, size_t length, size_t stride) {
    if (!r || !f || !dst) return P26_INVALID;
    pthread_mutex_lock(&r->mutex);
    size_t index = 0;
    p26_status st = P26_STALE;
    if (ticket_ok(r, f, &index)) {
        (void)index;
        size_t visible = (size_t)r->desc.width * 4U;
        st = P26_BOUNDS;
        if (stride >= visible &&
            (size_t)(r->desc.height - 1U) <= SIZE_MAX / stride &&
            (size_t)(r->desc.height - 1U) * stride <= SIZE_MAX - visible &&
            length >= (size_t)(r->desc.height - 1U) * stride + visible) {
            st = P26_OK;
            for (uint32_t y = 0; y < r->desc.height; ++y) {
                st = p26_read(r->device, f->frame, (size_t)y * r->desc.stride,
                              dst + (size_t)y * stride, visible);
                if (st != P26_OK) break;
            }
        }
    }
    pthread_mutex_unlock(&r->mutex);
    return st;
}
p26_status p27c_frame_release(p27c_renderer *r, p27c_frame *f) {
    if (!r || !f) return P26_INVALID;
    pthread_mutex_lock(&r->mutex);
    size_t index = 0;
    p26_status st = P26_STALE;
    if (ticket_ok(r, f, &index)) {
        st = p26_release(r->device, f->frame);
        if (st == P26_OK) {
            r->frames[index] = r->frames[--r->inflight];
            *f = (p27c_frame){0};
        }
    }
    pthread_mutex_unlock(&r->mutex);
    return st;
}
