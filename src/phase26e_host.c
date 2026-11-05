#include "ios27/phase26e_host.h"

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct p26_scene { phase26e_id id; };
struct p26_window { phase26e_id id, scene; phase26e_rect bounds; };
struct p26_view {
    phase26e_id id, window, parent;
    phase26e_rect rect;
    int32_t z;
};
struct p26_touch { int active; int32_t pointer; phase26e_id window, target; float start_x, start_y; };

struct phase26e_host {
    pthread_mutex_t lock;
    phase26e_metrics metrics;
    phase26e_id next_id;
    struct p26_scene scenes[PHASE26E_MAX_SCENES];
    struct p26_window windows[PHASE26E_MAX_WINDOWS];
    struct p26_view views[PHASE26E_MAX_VIEWS];
    struct p26_touch touches[PHASE26E_MAX_TOUCHES];
    phase26e_event events[PHASE26E_MAX_EVENT_LOG];
    size_t head, count;
    uint64_t seq;
    uint32_t width, height, rotation;
    int pending_frame;
};

static int rect_ok(phase26e_rect r) {
    return isfinite(r.x) && isfinite(r.y) && isfinite(r.width) &&
           isfinite(r.height) && r.width > 0 && r.height > 0 &&
           r.width <= 8192 && r.height <= 8192 &&
           fabsf(r.x) <= 32768 && fabsf(r.y) <= 32768;
}
static int point_in(phase26e_rect r, float x, float y) {
    return x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height;
}
static struct p26_window *window_by_id(phase26e_host *h, phase26e_id id) {
    for (size_t i = 0; i < PHASE26E_MAX_WINDOWS; ++i)
        if (h->windows[i].id && h->windows[i].id == id) return &h->windows[i];
    return NULL;
}
static struct p26_view *view_by_id(phase26e_host *h, phase26e_id id) {
    for (size_t i = 0; i < PHASE26E_MAX_VIEWS; ++i)
        if (h->views[i].id && h->views[i].id == id) return &h->views[i];
    return NULL;
}
static int scene_exists(phase26e_host *h, phase26e_id id) {
    for (size_t i = 0; i < PHASE26E_MAX_SCENES; ++i)
        if (h->scenes[i].id == id && id) return 1;
    return 0;
}
static int active(phase26e_host *h) {
    return h->metrics.lifecycle == P26_FOREGROUND && h->metrics.surface_active;
}
static void frame(phase26e_host *h) {
    if (active(h) && !h->pending_frame) {
        h->pending_frame = 1;
        ++h->metrics.frames_requested;
    }
}
static void log_event(phase26e_host *h, phase26e_event *e) {
    e->sequence = ++h->seq;
    if (h->count == PHASE26E_MAX_EVENT_LOG) {
        h->head = (h->head + 1u) % PHASE26E_MAX_EVENT_LOG;
        --h->count;
        ++h->metrics.dropped_log_events;
    }
    h->events[(h->head + h->count) % PHASE26E_MAX_EVENT_LOG] = *e;
    ++h->count;
    ++h->metrics.accepted_events;
    frame(h);
}
static void cancel_all(phase26e_host *h) {
    for (size_t i = 0; i < PHASE26E_MAX_TOUCHES; ++i) {
        struct p26_touch *t = &h->touches[i];
        if (!t->active) continue;
        phase26e_event e = {0};
        e.kind = P26_CANCEL;
        e.pointer = t->pointer;
        e.window = t->window;
        e.target = t->target;
        log_event(h, &e);
        t->active = 0;
        ++h->metrics.cancellations;
    }
    h->metrics.active_touches = 0;
}
static phase26e_result rejected(phase26e_host *h, phase26e_result r) {
    ++h->metrics.rejected_events;
    return r;
}
static phase26e_id hit(phase26e_host *h, phase26e_id window, float x, float y) {
    struct p26_window *w = window_by_id(h, window);
    if (!w || !point_in(w->bounds, x, y)) return 0;
    phase26e_id chosen = 0;
    int32_t best_z = INT32_MIN;
    for (size_t i = 0; i < PHASE26E_MAX_VIEWS; ++i) {
        struct p26_view *v = &h->views[i];
        if (!v->id || v->window != window) continue;
        struct p26_view *chain[PHASE26E_MAX_VIEWS];
        size_t depth = 0;
        struct p26_view *cur = v;
        while (cur && depth < PHASE26E_MAX_VIEWS) {
            chain[depth++] = cur;
            cur = cur->parent ? view_by_id(h, cur->parent) : NULL;
        }
        if (cur || !depth) continue;
        float ax = 0, ay = 0;
        int inside = 1;
        for (size_t j = depth; j > 0; --j) {
            cur = chain[j - 1];
            ax += cur->rect.x;
            ay += cur->rect.y;
            if (!point_in((phase26e_rect){ax, ay, cur->rect.width, cur->rect.height}, x, y)) {
                inside = 0;
                break;
            }
        }
        if (!inside) continue;
        if (!chosen || v->z > best_z || (v->z == best_z && v->id > chosen)) {
            chosen = v->id;
            best_z = v->z;
        }
    }
    return chosen;
}

phase26e_host *phase26e_create(void) {
    phase26e_host *h = (phase26e_host *)calloc(1, sizeof(*h));
    if (!h) return NULL;
    if (pthread_mutex_init(&h->lock, NULL)) { free(h); return NULL; }
    h->next_id = 1;
    return h;
}
void phase26e_destroy(phase26e_host *h) {
    if (!h) return;
    (void)phase26e_terminate(h);
    pthread_mutex_destroy(&h->lock);
    free(h);
}
phase26e_result phase26e_launch(phase26e_host *h) {
    if (!h) return P26_INVALID;
    pthread_mutex_lock(&h->lock);
    phase26e_result r = P26_STATE;
    if (h->metrics.lifecycle == P26_CREATED) {
        h->metrics.lifecycle = P26_FOREGROUND;
        ++h->metrics.lifecycle_transitions;
        frame(h);
        r = P26_OK;
    }
    pthread_mutex_unlock(&h->lock);
    return r;
}
phase26e_result phase26e_pause(phase26e_host *h) {
    if (!h) return P26_INVALID;
    pthread_mutex_lock(&h->lock);
    phase26e_result r = P26_STATE;
    if (h->metrics.lifecycle == P26_FOREGROUND) {
        cancel_all(h);
        h->metrics.lifecycle = P26_BACKGROUND;
        h->pending_frame = 0;
        ++h->metrics.lifecycle_transitions;
        r = P26_OK;
    }
    pthread_mutex_unlock(&h->lock);
    return r;
}
phase26e_result phase26e_resume(phase26e_host *h) {
    if (!h) return P26_INVALID;
    pthread_mutex_lock(&h->lock);
    phase26e_result r = P26_STATE;
    if (h->metrics.lifecycle == P26_BACKGROUND) {
        h->metrics.lifecycle = P26_FOREGROUND;
        ++h->metrics.lifecycle_transitions;
        frame(h);
        r = P26_OK;
    }
    pthread_mutex_unlock(&h->lock);
    return r;
}
phase26e_result phase26e_terminate(phase26e_host *h) {
    if (!h) return P26_INVALID;
    pthread_mutex_lock(&h->lock);
    if (h->metrics.lifecycle != P26_TERMINATED) {
        cancel_all(h);
        h->metrics.lifecycle = P26_TERMINATED;
        h->metrics.surface_active = 0;
        h->pending_frame = 0;
        ++h->metrics.lifecycle_transitions;
    }
    pthread_mutex_unlock(&h->lock);
    return P26_OK;
}
phase26e_result phase26e_surface(phase26e_host *h, int available, uint32_t width,
                                  uint32_t height, uint32_t rotation) {
    if (!h) return P26_INVALID;
    if ((available != 0 && available != 1) || (available &&
        (!width || !height || width > 8192 || height > 8192 ||
         (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270)))) return P26_INVALID;
    pthread_mutex_lock(&h->lock);
    if (h->metrics.lifecycle == P26_TERMINATED) { pthread_mutex_unlock(&h->lock); return P26_STATE; }
    if (!available || h->metrics.surface_active) cancel_all(h);
    ++h->metrics.surface_generation;
    ++h->metrics.surface_recreations;
    h->metrics.surface_active = available;
    h->width = available ? width : 0;
    h->height = available ? height : 0;
    h->rotation = available ? rotation : 0;
    h->pending_frame = 0;
    frame(h);
    pthread_mutex_unlock(&h->lock);
    return P26_OK;
}
phase26e_result phase26e_scene_add(phase26e_host *h, phase26e_id *out) {
    if (!h || !out) return P26_INVALID;
    pthread_mutex_lock(&h->lock);
    phase26e_result r = P26_FULL;
    if (h->metrics.lifecycle == P26_TERMINATED) r = P26_STATE;
    else for (size_t i = 0; i < PHASE26E_MAX_SCENES; ++i)
        if (!h->scenes[i].id) { *out = h->scenes[i].id = h->next_id++; r = P26_OK; break; }
    pthread_mutex_unlock(&h->lock);
    return r;
}
phase26e_result phase26e_window_add(phase26e_host *h, phase26e_id scene,
                                     phase26e_rect bounds, phase26e_id *out) {
    if (!h || !out || !rect_ok(bounds)) return P26_INVALID;
    pthread_mutex_lock(&h->lock);
    phase26e_result r = P26_NOT_FOUND;
    if (h->metrics.lifecycle == P26_TERMINATED) r = P26_STATE;
    else if (scene_exists(h, scene)) {
        r = P26_FULL;
        for (size_t i = 0; i < PHASE26E_MAX_WINDOWS; ++i) {
            if (!h->windows[i].id) {
                *out = h->windows[i].id = h->next_id++;
                h->windows[i].scene = scene;
                h->windows[i].bounds = bounds;
                r = P26_OK; break;
            }
        }
    }
    pthread_mutex_unlock(&h->lock);
    return r;
}
phase26e_result phase26e_view_add(phase26e_host *h, phase26e_id window,
                                   phase26e_id parent, phase26e_rect bounds,
                                   int32_t z, phase26e_id *out) {
    if (!h || !out || !rect_ok(bounds)) return P26_INVALID;
    pthread_mutex_lock(&h->lock);
    phase26e_result r = P26_NOT_FOUND;
    struct p26_view *p = parent ? view_by_id(h, parent) : NULL;
    if (h->metrics.lifecycle == P26_TERMINATED) r = P26_STATE;
    else if (window_by_id(h, window) && (!parent || (p && p->window == window))) {
        r = P26_FULL;
        for (size_t i = 0; i < PHASE26E_MAX_VIEWS; ++i) {
            if (!h->views[i].id) {
                *out = h->views[i].id = h->next_id++;
                h->views[i].window = window;
                h->views[i].parent = parent;
                h->views[i].rect = bounds;
                h->views[i].z = z;
                r = P26_OK; break;
            }
        }
    }
    pthread_mutex_unlock(&h->lock);
    return r;
}
phase26e_result phase26e_view_remove(phase26e_host *h, phase26e_id view) {
    if (!h || !view) return P26_INVALID;
    pthread_mutex_lock(&h->lock);
    if (h->metrics.lifecycle == P26_TERMINATED) { pthread_mutex_unlock(&h->lock); return P26_STATE; }
    struct p26_view *v = view_by_id(h, view);
    if (!v) { pthread_mutex_unlock(&h->lock); return P26_NOT_FOUND; }
    /* Removing a parent also retires every descendant and cancels captured touches. */
    uint8_t mark[PHASE26E_MAX_VIEWS] = {0};
    for (size_t i = 0; i < PHASE26E_MAX_VIEWS; ++i) if (h->views[i].id == view) mark[i] = 1;
    for (size_t pass = 0; pass < PHASE26E_MAX_VIEWS; ++pass) {
        int changed = 0;
        for (size_t i = 0; i < PHASE26E_MAX_VIEWS; ++i) {
            if (!h->views[i].id || mark[i]) continue;
            for (size_t j = 0; j < PHASE26E_MAX_VIEWS; ++j)
                if (mark[j] && h->views[j].id == h->views[i].parent) { mark[i] = 1; changed = 1; break; }
        }
        if (!changed) break;
    }
    for (size_t i = 0; i < PHASE26E_MAX_TOUCHES; ++i) if (h->touches[i].active) {
        for (size_t j = 0; j < PHASE26E_MAX_VIEWS; ++j) if (mark[j] && h->touches[i].target == h->views[j].id) {
            phase26e_event e = {0}; e.kind = P26_CANCEL; e.pointer = h->touches[i].pointer;
            e.window = h->touches[i].window; e.target = h->touches[i].target;
            log_event(h, &e);
            h->touches[i].active = 0;
            --h->metrics.active_touches;
            ++h->metrics.cancellations;
            break;
        }
    }
    for (size_t i = 0; i < PHASE26E_MAX_VIEWS; ++i) if (mark[i]) memset(&h->views[i], 0, sizeof(h->views[i]));
    pthread_mutex_unlock(&h->lock);
    return P26_OK;
}
phase26e_result phase26e_hit_test(phase26e_host *h, phase26e_id window,
                                   float x, float y, phase26e_id *out) {
    if (!h || !out || !isfinite(x) || !isfinite(y)) return P26_INVALID;
    pthread_mutex_lock(&h->lock);
    phase26e_result r = P26_NOT_FOUND;
    if (window_by_id(h, window)) { *out = hit(h, window, x, y); r = P26_OK; }
    pthread_mutex_unlock(&h->lock);
    return r;
}
size_t phase26e_responder_chain(phase26e_host *h, phase26e_id view,
                                phase26e_id *out, size_t cap) {
    if (!h || !out || !cap) return 0;
    pthread_mutex_lock(&h->lock);
    struct p26_view *v = view_by_id(h, view);
    size_t n = 0;
    phase26e_id window = v ? v->window : 0;
    while (v && n < cap && n < PHASE26E_MAX_VIEWS) {
        out[n++] = v->id;
        v = v->parent ? view_by_id(h, v->parent) : NULL;
    }
    struct p26_window *w = window_by_id(h, window);
    if (w && n < cap) out[n++] = w->id;
    if (w && n < cap) out[n++] = w->scene;
    pthread_mutex_unlock(&h->lock);
    return n;
}
phase26e_result phase26e_convert_to_window(phase26e_host *h, phase26e_id view,
                                            float x, float y, float *wx, float *wy) {
    if (!h || !wx || !wy || !isfinite(x) || !isfinite(y)) return P26_INVALID;
    pthread_mutex_lock(&h->lock);
    struct p26_view *v = view_by_id(h, view);
    if (!v) { pthread_mutex_unlock(&h->lock); return P26_NOT_FOUND; }
    unsigned depth = 0;
    while (v && depth < PHASE26E_MAX_VIEWS) {
        ++depth;
        x += v->rect.x; y += v->rect.y;
        v = view_by_id(h, v->parent);
    }
    /* Exactly MAX_VIEWS ancestors are valid. Only a remaining link is invalid. */
    phase26e_result r = (v != NULL || !isfinite(x) || !isfinite(y)) ? P26_INVALID : P26_OK;
    if (r == P26_OK) { *wx = x; *wy = y; }
    pthread_mutex_unlock(&h->lock);
    return r;
}
phase26e_result phase26e_pointer(phase26e_host *h, phase26e_event_kind kind,
                                  int32_t pointer, float x, float y) {
    if (!h || pointer < 0 || !isfinite(x) || !isfinite(y) ||
        (kind != P26_DOWN && kind != P26_MOVE && kind != P26_UP && kind != P26_CANCEL)) return P26_INVALID;
    pthread_mutex_lock(&h->lock);
    if (!active(h)) { phase26e_result r = rejected(h, P26_STATE); pthread_mutex_unlock(&h->lock); return r; }
    struct p26_touch *t = NULL, *empty = NULL;
    for (size_t i = 0; i < PHASE26E_MAX_TOUCHES; ++i) {
        if (h->touches[i].active && h->touches[i].pointer == pointer) t = &h->touches[i];
        if (!h->touches[i].active && !empty) empty = &h->touches[i];
    }
    if ((kind == P26_DOWN && (t || !empty)) || (kind != P26_DOWN && !t)) {
        phase26e_result r = rejected(h, (kind == P26_DOWN && !empty) ? P26_FULL : P26_INVALID);
        pthread_mutex_unlock(&h->lock); return r;
    }
    if (kind == P26_DOWN) {
        phase26e_id window = 0, target = 0;
        for (size_t i = PHASE26E_MAX_WINDOWS; i > 0; --i) {
            if (h->windows[i - 1].id && point_in(h->windows[i - 1].bounds, x, y)) {
                window = h->windows[i - 1].id;
                target = hit(h, window, x, y);
                break;
            }
        }
        if (!window) { phase26e_result r = rejected(h, P26_NOT_FOUND); pthread_mutex_unlock(&h->lock); return r; }
        t = empty;
        *t = (struct p26_touch){1, pointer, window, target, x, y};
        ++h->metrics.active_touches;
    }
    phase26e_event e = {0};
    e.kind = kind; e.pointer = pointer; e.x = x; e.y = y;
    e.window = t->window; e.target = t->target;
    log_event(h, &e);
    if (kind == P26_UP) {
        float dx = x - t->start_x, dy = y - t->start_y;
        if (dx * dx + dy * dy <= 100.0f && t->target) {
            e.kind = P26_TAP;
            log_event(h, &e);
        }
    }
    if (kind == P26_UP || kind == P26_CANCEL) {
        t->active = 0; --h->metrics.active_touches;
        if (kind == P26_CANCEL) ++h->metrics.cancellations;
    }
    pthread_mutex_unlock(&h->lock);
    return P26_OK;
}
phase26e_result phase26e_keyboard(phase26e_host *h, int down, int32_t code) {
    if (!h || (down != 0 && down != 1) || code < 0 || code > 10000) return P26_INVALID;
    pthread_mutex_lock(&h->lock);
    if (!active(h)) { phase26e_result r = rejected(h, P26_STATE); pthread_mutex_unlock(&h->lock); return r; }
    phase26e_event e = {0}; e.kind = down ? P26_KEY_DOWN : P26_KEY_UP; e.keycode = code;
    log_event(h, &e);
    pthread_mutex_unlock(&h->lock);
    return P26_OK;
}
/* Reject malformed UTF-8, overlong sequences, surrogates and non-Unicode scalars. */
static int utf8_ok(const unsigned char *s, size_t n) {
    for (size_t i = 0; i < n;) {
        uint32_t c = s[i++];
        unsigned extra = 0;
        if (c < 0x80) continue;
        if (c >= 0xC2 && c <= 0xDF) { c &= 31; extra = 1; }
        else if (c >= 0xE0 && c <= 0xEF) { c &= 15; extra = 2; }
        else if (c >= 0xF0 && c <= 0xF4) { c &= 7; extra = 3; }
        else return 0;
        if (extra > n - i) return 0;
        unsigned k = extra;
        while (k--) { if ((s[i] & 0xC0) != 0x80) return 0; c = (c << 6) | (s[i++] & 63); }
        if ((extra == 1 && c < 0x80) || (extra == 2 && c < 0x800) ||
            (extra == 3 && c < 0x10000) || (c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF) return 0;
    }
    return 1;
}
static phase26e_result text_event(phase26e_host *h, const char *utf8, size_t n, phase26e_event_kind kind) {
    if (!h || !utf8 || n > PHASE26E_MAX_TEXT || memchr(utf8, 0, n) ||
        !utf8_ok((const unsigned char *)utf8, n)) return P26_INVALID;
    pthread_mutex_lock(&h->lock);
    if (!active(h)) { phase26e_result r = rejected(h, P26_STATE); pthread_mutex_unlock(&h->lock); return r; }
    phase26e_event e = {0}; e.kind = kind;
    memcpy(e.text, utf8, n); e.text[n] = '\0';
    log_event(h, &e);
    pthread_mutex_unlock(&h->lock);
    return P26_OK;
}
phase26e_result phase26e_text(phase26e_host *h, const char *text, size_t n) {
    return text_event(h, text, n, P26_TEXT);
}
phase26e_result phase26e_compose(phase26e_host *h, const char *text, size_t n) {
    return text_event(h, text, n, P26_COMPOSE);
}
phase26e_result phase26e_focus(phase26e_host *h, int focused) {
    if (!h || (focused != 0 && focused != 1)) return P26_INVALID;
    pthread_mutex_lock(&h->lock);
    if (h->metrics.lifecycle == P26_TERMINATED) { pthread_mutex_unlock(&h->lock); return P26_STATE; }
    if (!focused) {
        cancel_all(h);
        phase26e_event e = {0}; e.kind = P26_FOCUS_LOST;
        log_event(h, &e);
    }
    pthread_mutex_unlock(&h->lock);
    return P26_OK;
}
phase26e_result phase26e_accessibility_focus(phase26e_host *h, phase26e_id view) {
    if (!h || !view) return P26_INVALID;
    pthread_mutex_lock(&h->lock);
    if (!active(h)) { phase26e_result r = rejected(h, P26_STATE); pthread_mutex_unlock(&h->lock); return r; }
    struct p26_view *v = view_by_id(h, view);
    if (!v) { phase26e_result r = rejected(h, P26_NOT_FOUND); pthread_mutex_unlock(&h->lock); return r; }
    phase26e_event e = {0}; e.kind = P26_A11Y_FOCUS; e.target = view; e.window = v->window;
    log_event(h, &e);
    pthread_mutex_unlock(&h->lock);
    return P26_OK;
}
phase26e_result phase26e_service(phase26e_host *h, const char *service) {
    if (!h || !service) return P26_INVALID;
    /* No host capability is accidentally exposed, including audio and permissions. */
    return P26_DENIED;
}
phase26e_result phase26e_request_frame(phase26e_host *h) {
    if (!h) return P26_INVALID;
    pthread_mutex_lock(&h->lock);
    phase26e_result r = active(h) ? P26_OK : P26_STATE;
    frame(h);
    pthread_mutex_unlock(&h->lock);
    return r;
}
phase26e_result phase26e_present(phase26e_host *h, uint64_t generation) {
    if (!h) return P26_INVALID;
    pthread_mutex_lock(&h->lock);
    phase26e_result r = P26_STATE;
    if (active(h) && generation == h->metrics.surface_generation && h->pending_frame) {
        h->pending_frame = 0;
        ++h->metrics.frames_presented;
        r = P26_OK;
    }
    pthread_mutex_unlock(&h->lock);
    return r;
}
size_t phase26e_drain(phase26e_host *h, phase26e_event *out, size_t cap) {
    if (!h || (!out && cap)) return 0;
    pthread_mutex_lock(&h->lock);
    size_t n = h->count < cap ? h->count : cap;
    for (size_t i = 0; i < n; ++i) out[i] = h->events[(h->head + i) % PHASE26E_MAX_EVENT_LOG];
    h->head = (h->head + n) % PHASE26E_MAX_EVENT_LOG;
    h->count -= n;
    pthread_mutex_unlock(&h->lock);
    return n;
}
phase26e_metrics phase26e_get_metrics(phase26e_host *h) {
    phase26e_metrics m = {0};
    if (h) { pthread_mutex_lock(&h->lock); m = h->metrics; pthread_mutex_unlock(&h->lock); }
    return m;
}
