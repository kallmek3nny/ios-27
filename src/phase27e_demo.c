#include "ios27/phase27e_demo.h"
#include <math.h>
#include <string.h>

void p27_scene_init(p27_scene *s) { if (s) memset(s, 0, sizeof(*s)); }

int p27_scene_pointer(p27_scene *s, int kind, int32_t id,
                      float x, float y, uint64_t now_ms) {
    if (!s || id < 0 || (kind < 1 || kind > 4) || !isfinite(x) || !isfinite(y) ||
        x < 0.f || x > 1.f || y < 0.f || y > 1.f) return 0;
    p27_pointer *found = NULL, *empty = NULL;
    for (unsigned i = 0; i < P27_MAX_POINTERS; ++i) {
        if (s->pointers[i].active && s->pointers[i].id == id) found = &s->pointers[i];
        if (!s->pointers[i].active && !empty) empty = &s->pointers[i];
    }
    if (kind == 1) {
        if (found || !empty) return 0;
        found = empty;
        found->active = 1;
        found->id = id;
    } else if (!found) return 0;
    found->x = x;
    found->y = y;
    s->last_x = x;
    s->last_y = y;
    s->last_input_ms = now_ms;
    ++s->input_count;
    if (kind == 3) ++s->tap_count;
    if (kind >= 3) found->active = 0;
    return 1;
}

static uint8_t mix(uint8_t dst, uint8_t src, unsigned alpha) {
    return (uint8_t)(((unsigned)dst * (255u - alpha) + (unsigned)src * alpha + 127u) / 255u);
}
static void overlay(uint8_t *p, uint8_t r, uint8_t g, uint8_t b, unsigned a) {
    p[0] = mix(p[0], r, a);
    p[1] = mix(p[1], g, a);
    p[2] = mix(p[2], b, a);
}
static int round_rect(int32_t x, int32_t y, int32_t l, int32_t t,
                      int32_t r, int32_t b, int32_t rad) {
    if (x < l || x >= r || y < t || y >= b) return 0;
    int32_t cx = x < l + rad ? l + rad : x >= r - rad ? r - rad - 1 : x;
    int32_t cy = y < t + rad ? t + rad : y >= b - rad ? b - rad - 1 : y;
    int64_t dx = (int64_t)x - cx, dy = (int64_t)y - cy;
    return dx * dx + dy * dy <= (int64_t)rad * rad;
}
int p27_scene_render(const p27_scene *s, uint8_t *out, size_t cap,
                     uint32_t w, uint32_t h, uint32_t stride, uint64_t now_ms) {
    if (!s || !out || !w || !h || w > 8192u || h > 8192u ||
        (uint64_t)w * h > P27_MAX_PIXELS || stride < (uint64_t)w * 4u ||
        stride > 8192u * 4u || (size_t)stride * h > cap) return 0;
    int32_t panel_l = (int32_t)(w / 11u), panel_r = (int32_t)(w - w / 11u);
    int32_t panel_t = (int32_t)(h / 4u), panel_b = (int32_t)(h * 3u / 4u);
    int32_t rad = (int32_t)(w / 15u); if (rad > 52) rad = 52;
    int32_t size = (int32_t)(w < h ? w : h);
    int64_t bubble_r = size / 9; if (bubble_r < 14) bubble_r = 14;
    uint64_t age = now_ms >= s->last_input_ms ? now_ms - s->last_input_ms : UINT64_MAX;
    for (uint32_t y = 0; y < h; ++y) {
        uint8_t *row = out + (size_t)stride * y;
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t *p = row + (size_t)x * 4u;
            p[0] = (uint8_t)(19u + 32u * x / w);
            p[1] = (uint8_t)(23u + 14u * y / h);
            p[2] = (uint8_t)(49u + 40u * y / h + 12u * x / w);
            p[3] = 255u;
            /* Concentric translucent panels prove real CPU image composition. */
            if (round_rect((int32_t)x, (int32_t)y, panel_l, panel_t, panel_r, panel_b, rad)) {
                overlay(p, 88u, 100u, 156u, 100u);
                if (round_rect((int32_t)x, (int32_t)y, panel_l + rad / 2,
                               panel_t + rad / 2, panel_r - rad / 2,
                               panel_b - rad / 2, rad / 2))
                    overlay(p, 19u, 28u, 57u, 110u);
            }
            int64_t dx = (int64_t)x - (int64_t)((double)w * s->last_x);
            int64_t dy = (int64_t)y - (int64_t)((double)h * s->last_y);
            int64_t d2 = dx * dx + dy * dy;
            if (s->input_count && age < 700u) {
                int64_t radius = bubble_r + (int64_t)(age * (uint64_t)size / 2400u);
                if (d2 <= radius * radius)
                    overlay(p, s->tap_count % 2u ? 255u : 125u,
                            172u, s->tap_count % 2u ? 185u : 255u,
                            (unsigned)(155u * (700u - age) / 700u));
            }
            for (unsigned j = 0; j < P27_MAX_POINTERS; ++j) {
                const p27_pointer *touch = &s->pointers[j];
                if (!touch->active) continue;
                int64_t tx = (int64_t)x - (int64_t)((double)w * touch->x);
                int64_t ty = (int64_t)y - (int64_t)((double)h * touch->y);
                if (tx * tx + ty * ty <= bubble_r * bubble_r)
                    overlay(p, 104u, 236u, 245u, 190u);
            }
        }
    }
    return 1;
}

uint64_t p27_frame_hash(const uint8_t *bytes, size_t n) {
    if (!bytes) return 0;
    uint64_t h = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < n; ++i) { h ^= bytes[i]; h *= UINT64_C(1099511628211); }
    return h;
}
