#include "ios27/phase26c_graphics.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define P26_SLOTS 512U
#define P26_MAX_CMDS 64U
#define P26_LIMIT (128U * 1024U * 1024U)

typedef struct {
    unsigned type;
    p26_handle src, dst;
    size_t src_offset, dst_offset, length, pitch;
} p26_op;
typedef struct {
    p26_kind kind;
    uint32_t generation, refs, public_refs;
    uint64_t map_token, sequence;
    int locked;
    void *allocation;
    unsigned char *data;
    size_t bytes;
    p26_image_desc image;
    p26_op ops[P26_MAX_CMDS];
    size_t op_count;
} p26_slot;
struct p26_device {
    pthread_mutex_t mu;
    uint16_t id;
    int retiring, lost;
    size_t budget, used;
    uint64_t next_map_token;
    p26_slot slots[P26_SLOTS];
};
static atomic_uint p26_device_ids = ATOMIC_VAR_INIT(1);

static int add_ok(size_t a, size_t b, size_t *result) {
    if (b > SIZE_MAX - a) return 0;
    *result = a + b;
    return 1;
}
static int mul_ok(size_t a, size_t b, size_t *result) {
    if (a && b > SIZE_MAX / a) return 0;
    *result = a * b;
    return 1;
}
static int range_ok(size_t size, size_t offset, size_t length) {
    return offset <= size && length <= size - offset;
}
static int power2(size_t n) { return n && !(n & (n - 1U)); }
static unsigned bpp(p26_format fmt) {
    if (fmt == P26_R8) return 1U;
    if (fmt == P26_RGBA8 || fmt == P26_BGRA8) return 4U;
    return 0U;
}
static int layout(p26_image_desc d, size_t *bytes) {
    size_t visible;
    unsigned pixel = bpp(d.format);
    if (!pixel || !d.width || !d.height || d.width > 8192U ||
        d.height > 8192U || d.stride > 1048576U ||
        !mul_ok(d.width, pixel, &visible) || d.stride < visible ||
        (d.stride % pixel) != 0U || (d.stride % 16U) != 0U ||
        !mul_ok(d.stride, d.height, bytes) || *bytes > P26_LIMIT) return 0;
    return 1;
}
static p26_handle handle_of(p26_device *d, unsigned index) {
    return ((uint64_t)d->id << 48) |
           ((uint64_t)d->slots[index].generation << 16) | (uint64_t)(index + 1U);
}
static p26_slot *lookup(p26_device *d, p26_handle handle) {
    unsigned slot = (unsigned)(handle & 0xffffU);
    uint32_t generation = (uint32_t)((handle >> 16) & UINT32_MAX);
    if (!d || (handle >> 48) != d->id || !slot || slot > P26_SLOTS ||
        !generation) return NULL;
    p26_slot *s = &d->slots[slot - 1U];
    return (s->kind && s->generation == generation) ? s : NULL;
}
static p26_status alloc_slot(p26_device *d, p26_kind kind, size_t bytes,
                             size_t alignment, p26_handle *out) {
    if (!out || d->retiring) return P26_INVALID;
    if (d->lost) return P26_DEVICE_LOST;
    if (bytes > d->budget - d->used) return P26_EXHAUSTED;
    for (unsigned i = 0; i < P26_SLOTS; ++i) {
        p26_slot *s = &d->slots[i];
        if (s->kind || s->generation == UINT32_MAX) continue;
        if (!s->generation) s->generation = 1U;
        if (bytes) {
            size_t total;
            if (!add_ok(bytes, alignment - 1U, &total)) return P26_BOUNDS;
            s->allocation = calloc(1, total);
            if (!s->allocation) return P26_EXHAUSTED;
            uintptr_t p = (uintptr_t)s->allocation;
            s->data = (unsigned char *)((p + alignment - 1U) & ~(uintptr_t)(alignment - 1U));
        }
        s->kind = kind;
        s->refs = 1U;
        s->public_refs = 1U;
        s->bytes = bytes;
        d->used += bytes;
        *out = handle_of(d, i);
        return P26_OK;
    }
    return P26_EXHAUSTED;
}
static void release_slot(p26_device *d, p26_slot *s) {
    if (--s->refs) return;
    p26_op ops[P26_MAX_CMDS];
    size_t count = s->kind == P26_COMMAND ? s->op_count : 0U;
    if (count) memcpy(ops, s->ops, count * sizeof(*ops));
    d->used -= s->bytes;
    free(s->allocation);
    uint32_t generation = s->generation;
    memset(s, 0, sizeof(*s));
    s->generation = generation + 1U;
    /* The slots below stay alive via the pins taken when recording ops. */
    for (size_t i = 0; i < count; ++i) {
        p26_slot *src = lookup(d, ops[i].src);
        p26_slot *dst = lookup(d, ops[i].dst);
        if (src) release_slot(d, src);
        if (dst) release_slot(d, dst);
    }
}
static p26_status ref_slot(p26_slot *s) {
    if (s->refs == UINT32_MAX) return P26_EXHAUSTED;
    ++s->refs;
    return P26_OK;
}

p26_device *p26_device_create(size_t budget_bytes) {
    unsigned id = atomic_fetch_add_explicit(&p26_device_ids, 1U, memory_order_relaxed);
    if (!id || id > UINT16_MAX || !budget_bytes || budget_bytes > SIZE_MAX / 2U) return NULL;
    p26_device *d = calloc(1, sizeof(*d));
    if (!d) return NULL;
    if (pthread_mutex_init(&d->mu, NULL) != 0) { free(d); return NULL; }
    d->id = (uint16_t)id;
    d->budget = budget_bytes;
    return d;
}
p26_status p26_device_lose(p26_device *d) {
    if (!d) return P26_INVALID;
    pthread_mutex_lock(&d->mu);
    d->lost = 1;
    pthread_mutex_unlock(&d->mu);
    return P26_OK;
}
p26_status p26_device_shutdown(p26_device *d) {
    if (!d) return P26_INVALID;
    pthread_mutex_lock(&d->mu);
    for (unsigned i = 0; i < P26_SLOTS; ++i) {
        if (d->slots[i].kind) { pthread_mutex_unlock(&d->mu); return P26_BUSY; }
    }
    d->retiring = 1;
    pthread_mutex_unlock(&d->mu);
    return P26_OK;
}
void p26_device_destroy(p26_device *d) {
    if (!d) return;
    /* Destruction is permitted only following successful shutdown. */
    pthread_mutex_lock(&d->mu);
    int safe = d->retiring;
    pthread_mutex_unlock(&d->mu);
    if (!safe) return;
    pthread_mutex_destroy(&d->mu);
    free(d);
}
size_t p26_device_used(p26_device *d) {
    if (!d) return 0U;
    pthread_mutex_lock(&d->mu);
    size_t n = d->used;
    pthread_mutex_unlock(&d->mu);
    return n;
}
p26_status p26_buffer_create(p26_device *d, size_t bytes,
                             size_t alignment, p26_handle *out) {
    if (!d || !bytes || bytes > P26_LIMIT || !power2(alignment) || alignment > 4096U)
        return P26_INVALID;
    pthread_mutex_lock(&d->mu);
    p26_status status = alloc_slot(d, P26_BUFFER, bytes, alignment, out);
    pthread_mutex_unlock(&d->mu);
    return status;
}
p26_status p26_image_create(p26_device *d, p26_kind kind,
                            p26_image_desc desc, p26_handle *out) {
    size_t bytes = 0;
    if (!d || (kind != P26_TEXTURE && kind != P26_SURFACE) || !layout(desc, &bytes))
        return P26_INVALID;
    pthread_mutex_lock(&d->mu);
    p26_status status = alloc_slot(d, kind, bytes, 64U, out);
    if (status == P26_OK) lookup(d, *out)->image = desc;
    pthread_mutex_unlock(&d->mu);
    return status;
}
static int is_image(p26_kind k) { return k == P26_TEXTURE || k == P26_SURFACE; }
p26_status p26_image_info(p26_device *d, p26_handle image, p26_image_desc *desc) {
    if (!d || !desc) return P26_INVALID;
    pthread_mutex_lock(&d->mu);
    p26_slot *s = lookup(d, image);
    p26_status st = P26_STALE;
    if (s && s->public_refs) {
        st = is_image(s->kind) ? P26_OK : P26_INVALID;
        if (st == P26_OK) *desc = s->image;
    }
    pthread_mutex_unlock(&d->mu);
    return st;
}
static p26_status empty_slot(p26_device *d, p26_kind kind, p26_handle *out) {
    if (!d) return P26_INVALID;
    pthread_mutex_lock(&d->mu);
    p26_status status = alloc_slot(d, kind, 0, 1U, out);
    pthread_mutex_unlock(&d->mu);
    return status;
}
p26_status p26_queue_create(p26_device *d, p26_handle *out) { return empty_slot(d, P26_QUEUE, out); }
p26_status p26_command_create(p26_device *d, p26_handle *out) { return empty_slot(d, P26_COMMAND, out); }
p26_status p26_retain(p26_device *d, p26_handle h) {
    if (!d) return P26_INVALID;
    pthread_mutex_lock(&d->mu);
    p26_slot *s = lookup(d, h);
    p26_status status = (s && s->public_refs) ? ref_slot(s) : P26_STALE;
    if (status == P26_OK) ++s->public_refs;
    pthread_mutex_unlock(&d->mu);
    return status;
}
p26_status p26_release(p26_device *d, p26_handle h) {
    if (!d) return P26_INVALID;
    pthread_mutex_lock(&d->mu);
    p26_slot *s = lookup(d, h);
    p26_status result = (s && s->public_refs) ? P26_OK : P26_STALE;
    if (result == P26_OK) { --s->public_refs; release_slot(d, s); }
    pthread_mutex_unlock(&d->mu);
    return result;
}
p26_status p26_resource_lock(p26_device *d, p26_handle h, p26_mapping *out) {
    if (!d || !out) return P26_INVALID;
    pthread_mutex_lock(&d->mu);
    p26_slot *s = lookup(d, h);
    p26_status st = P26_STALE;
    if (s && s->public_refs) {
        st = d->lost ? P26_DEVICE_LOST :
             ((s->kind == P26_FRAME) ? P26_INVALID :
             ((!s->data || s->locked) ? P26_BUSY : ref_slot(s)));
        if (st == P26_OK) {
            s->locked = 1;
            s->map_token = ++d->next_map_token;
            if (!s->map_token) s->map_token = ++d->next_map_token;
            *out = (p26_mapping){ h, s->map_token, s->data, s->bytes };
        }
    }
    pthread_mutex_unlock(&d->mu);
    return st;
}
p26_status p26_resource_unlock(p26_device *d, p26_mapping *m) {
    if (!d || !m) return P26_INVALID;
    pthread_mutex_lock(&d->mu);
    p26_slot *s = lookup(d, m->handle);
    p26_status st = P26_STALE;
    if (s && s->locked && s->map_token == m->token && s->data == m->bytes &&
        s->bytes == m->length && m->token) {
        s->locked = 0;
        s->map_token = 0;
        release_slot(d, s);
        *m = (p26_mapping){0};
        st = P26_OK;
    }
    pthread_mutex_unlock(&d->mu);
    return st;
}
static p26_status transfer(p26_device *d, p26_handle h, size_t off,
                           void *data, size_t len, int writing) {
    if (!d || (len && !data)) return P26_INVALID;
    pthread_mutex_lock(&d->mu);
    p26_slot *s = lookup(d, h);
    p26_status st = P26_STALE;
    if (s && s->public_refs) {
        if (d->lost && s->kind != P26_FRAME) st = P26_DEVICE_LOST;
        else if (!s->data || (writing && s->kind == P26_FRAME)) st = P26_INVALID;
        else if (s->locked) st = P26_BUSY;
        else if (!range_ok(s->bytes, off, len)) st = P26_BOUNDS;
        else {
            if (len) {
                if (writing) memcpy(s->data + off, data, len);
                else memcpy(data, s->data + off, len);
            }
            st = P26_OK;
        }
    }
    pthread_mutex_unlock(&d->mu);
    return st;
}
p26_status p26_write(p26_device *d, p26_handle h, size_t off,
                     const void *data, size_t len) {
    return transfer(d, h, off, (void *)data, len, 1);
}
p26_status p26_read(p26_device *d, p26_handle h, size_t off,
                    void *data, size_t len) {
    return transfer(d, h, off, data, len, 0);
}
static p26_status record(p26_device *d, p26_handle command, p26_op op) {
    if (!d) return P26_INVALID;
    pthread_mutex_lock(&d->mu);
    p26_slot *cmd = lookup(d, command);
    p26_slot *src = lookup(d, op.src);
    p26_slot *dst = lookup(d, op.dst);
    p26_status st = P26_STALE;
    if (cmd && src && dst && cmd->public_refs && src->public_refs && dst->public_refs) {
        st = d->lost ? P26_DEVICE_LOST : P26_INVALID;
        int valid_types = (op.type == 1U && src->kind == P26_BUFFER && dst->kind == P26_BUFFER) ||
            (op.type == 2U && is_image(src->kind) && is_image(dst->kind)) ||
            (op.type == 3U && src->kind == P26_BUFFER && is_image(dst->kind));
        if (!d->lost && valid_types && cmd->kind == P26_COMMAND && cmd->op_count < P26_MAX_CMDS) {
            st = ref_slot(src);
            if (st == P26_OK) {
                st = ref_slot(dst);
                if (st != P26_OK) release_slot(d, src);
                else cmd->ops[cmd->op_count++] = op;
            }
        } else if (!d->lost && valid_types && cmd->kind == P26_COMMAND) st = P26_EXHAUSTED;
    }
    pthread_mutex_unlock(&d->mu);
    return st;
}
p26_status p26_command_copy(p26_device *d, p26_handle command,
                            p26_handle src, p26_handle dst,
                            size_t src_off, size_t dst_off, size_t bytes) {
    return record(d, command, (p26_op){1U, src, dst, src_off, dst_off, bytes, 0});
}
p26_status p26_command_blit(p26_device *d, p26_handle command,
                            p26_handle src, p26_handle dst) {
    return record(d, command, (p26_op){2U, src, dst, 0, 0, 0, 0});
}
p26_status p26_command_upload(p26_device *d, p26_handle command,
                              p26_handle src, p26_handle dst,
                              size_t offset, size_t stride) {
    return record(d, command, (p26_op){3U, src, dst, offset, 0, 0, stride});
}
static p26_status validate(p26_slot *src, p26_slot *dst, p26_op op) {
    if (!src || !dst) return P26_STALE;
    if (src->locked || dst->locked) return P26_BUSY;
    if (op.type == 1U) {
        if (src->kind != P26_BUFFER || dst->kind != P26_BUFFER) return P26_INVALID;
        return (range_ok(src->bytes, op.src_offset, op.length) &&
                range_ok(dst->bytes, op.dst_offset, op.length)) ? P26_OK : P26_BOUNDS;
    }
    if (!is_image(dst->kind)) return P26_INVALID;
    p26_image_desc b = dst->image;
    size_t visible = (size_t)b.width * bpp(b.format);
    if (op.type == 2U) {
        p26_image_desc a = src->image;
        if (!is_image(src->kind) || a.width != b.width || a.height != b.height ||
            (bpp(a.format) != bpp(b.format)) ||
            (bpp(a.format) == 1U && a.format != b.format)) return P26_UNSUPPORTED;
        return P26_OK;
    }
    if (op.type == 3U) {
        size_t bytes;
        if (src->kind != P26_BUFFER || op.pitch < visible ||
            !mul_ok(op.pitch, (size_t)b.height - 1U, &bytes) ||
            !add_ok(bytes, visible, &bytes) ||
            !range_ok(src->bytes, op.src_offset, bytes)) return P26_BOUNDS;
        return P26_OK;
    }
    return P26_UNSUPPORTED;
}
static void execute(p26_slot *src, p26_slot *dst, p26_op op) {
    if (op.type == 1U) {
        memmove(dst->data + op.dst_offset, src->data + op.src_offset, op.length);
        return;
    }
    p26_image_desc a = src->image, b = dst->image;
    unsigned pixel = bpp(b.format);
    size_t row_bytes = (size_t)b.width * pixel;
    for (size_t y = 0; y < b.height; ++y) {
        const unsigned char *from = src->data + (op.type == 3U ?
            op.src_offset + y * op.pitch : y * a.stride);
        unsigned char *to = dst->data + y * b.stride;
        if (op.type == 2U && a.format != b.format) {
            for (size_t x = 0; x < b.width; ++x) {
                const unsigned char *v = from + x * 4U;
                unsigned char *w = to + x * 4U;
                w[0] = v[2]; w[1] = v[1]; w[2] = v[0]; w[3] = v[3];
            }
        } else memmove(to, from, row_bytes);
    }
}
p26_status p26_submit(p26_device *d, p26_handle queue, p26_handle command, p26_fence *out) {
    if (!d || !out) return P26_INVALID;
    pthread_mutex_lock(&d->mu);
    p26_slot *q = lookup(d, queue), *cmd = lookup(d, command);
    p26_status st = P26_STALE;
    if (q && cmd && q->public_refs && cmd->public_refs) {
        st = d->lost ? P26_DEVICE_LOST : P26_INVALID;
        if (!d->lost && q->kind == P26_QUEUE && cmd->kind == P26_COMMAND && q->sequence != UINT64_MAX) {
            st = P26_OK;
            for (size_t i = 0; i < cmd->op_count; ++i) {
                p26_op op = cmd->ops[i];
                st = validate(lookup(d, op.src), lookup(d, op.dst), op);
                if (st != P26_OK) break;
            }
            if (st == P26_OK) {
                for (size_t i = 0; i < cmd->op_count; ++i) {
                    p26_op op = cmd->ops[i];
                    execute(lookup(d, op.src), lookup(d, op.dst), op);
                }
                *out = (p26_fence){queue, ++q->sequence};
                /* Clear command before dropping the pinned resource references. */
                size_t count = cmd->op_count;
                cmd->op_count = 0;
                for (size_t i = 0; i < count; ++i) {
                    p26_op op = cmd->ops[i];
                    release_slot(d, lookup(d, op.src));
                    release_slot(d, lookup(d, op.dst));
                }
            }
        }
    }
    pthread_mutex_unlock(&d->mu);
    return st;
}
p26_status p26_fence_poll(p26_device *d, p26_fence fence) {
    if (!d) return P26_INVALID;
    pthread_mutex_lock(&d->mu);
    p26_slot *q = lookup(d, fence.queue);
    p26_status st = P26_STALE;
    if (q && q->public_refs) st = d->lost ? P26_DEVICE_LOST :
        (q->kind != P26_QUEUE || !fence.sequence ? P26_INVALID :
        (fence.sequence <= q->sequence ? P26_OK : P26_PENDING));
    pthread_mutex_unlock(&d->mu);
    return st;
}
static uint64_t image_hash(p26_slot *s) {
    uint64_t hash = UINT64_C(14695981039346656037);
    p26_image_desc d = s->image;
    const uint32_t props[] = {d.width, d.height, (uint32_t)d.format};
    for (size_t i = 0; i < 3; ++i) {
        for (unsigned j = 0; j < 4; ++j) {
            hash = (hash ^ ((props[i] >> (j * 8U)) & 255U)) * UINT64_C(1099511628211);
        }
    }
    for (size_t y = 0; y < d.height; ++y) {
        const unsigned char *p = s->data + y * d.stride;
        for (size_t x = 0; x < (size_t)d.width * bpp(d.format); ++x)
            hash = (hash ^ p[x]) * UINT64_C(1099511628211);
    }
    return hash;
}
p26_status p26_present(p26_device *d, p26_handle image, p26_handle *frame) {
    if (!d || !frame) return P26_INVALID;
    pthread_mutex_lock(&d->mu);
    p26_slot *src = lookup(d, image);
    p26_status st = P26_STALE;
    if (src && src->public_refs) {
        st = P26_INVALID;
        if (d->lost) st = P26_DEVICE_LOST;
        else if (is_image(src->kind) && !src->locked) {
            st = alloc_slot(d, P26_FRAME, src->bytes, 64U, frame);
            if (st == P26_OK) {
                p26_slot *dst = lookup(d, *frame);
                dst->image = src->image;
                memcpy(dst->data, src->data, src->bytes);
                dst->sequence = image_hash(dst);
            }
        } else if (src->locked) st = P26_BUSY;
    }
    pthread_mutex_unlock(&d->mu);
    return st;
}
p26_status p26_frame_info(p26_device *d, p26_handle frame,
                          p26_image_desc *desc, uint64_t *hash) {
    if (!d || !desc || !hash) return P26_INVALID;
    pthread_mutex_lock(&d->mu);
    p26_slot *s = lookup(d, frame);
    p26_status st = P26_STALE;
    if (s && s->public_refs) {
        st = s->kind == P26_FRAME ? P26_OK : P26_INVALID;
        if (st == P26_OK) { *desc = s->image; *hash = s->sequence; }
    }
    pthread_mutex_unlock(&d->mu);
    return st;
}
