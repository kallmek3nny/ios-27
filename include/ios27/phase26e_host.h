#ifndef IOS27_PHASE26E_HOST_H
#define IOS27_PHASE26E_HOST_H

/* Independent, bounded Android-hosted scene fixture interface. Not UIKit. */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PHASE26E_MAX_SCENES 4u
#define PHASE26E_MAX_WINDOWS 8u
#define PHASE26E_MAX_VIEWS 64u
#define PHASE26E_MAX_TOUCHES 16u
#define PHASE26E_MAX_EVENT_LOG 512u
#define PHASE26E_MAX_TEXT 127u

typedef struct phase26e_host phase26e_host;
typedef uint64_t phase26e_id;

typedef enum {
    P26_OK = 0,
    P26_INVALID = 1,
    P26_STATE = 2,
    P26_FULL = 3,
    P26_NOT_FOUND = 4,
    P26_DENIED = 5
} phase26e_result;

typedef enum {
    P26_CREATED = 0,
    P26_FOREGROUND = 1,
    P26_BACKGROUND = 2,
    P26_TERMINATED = 3
} phase26e_lifecycle;

typedef enum {
    P26_DOWN = 1,
    P26_MOVE = 2,
    P26_UP = 3,
    P26_CANCEL = 4,
    P26_TAP = 5,
    P26_KEY_DOWN = 6,
    P26_KEY_UP = 7,
    P26_TEXT = 8,
    P26_A11Y_FOCUS = 9,
    P26_FOCUS_LOST = 10,
    P26_COMPOSE = 11
} phase26e_event_kind;

typedef struct {
    float x, y, width, height;
} phase26e_rect;

typedef struct {
    phase26e_event_kind kind;
    int32_t pointer;
    float x, y;
    int32_t keycode;
    uint64_t sequence;
    phase26e_id window, target;
    char text[PHASE26E_MAX_TEXT + 1u];
} phase26e_event;

typedef struct {
    uint64_t accepted_events, rejected_events, dropped_log_events;
    uint64_t cancellations, frames_requested, frames_presented;
    uint64_t lifecycle_transitions, surface_recreations;
    uint64_t active_touches;
    uint64_t surface_generation;
    phase26e_lifecycle lifecycle;
    int surface_active;
} phase26e_metrics;

/* Lifetime: join external callers after terminate, BEFORE destroy. */
phase26e_host *phase26e_create(void);
void phase26e_destroy(phase26e_host *host);
phase26e_result phase26e_launch(phase26e_host *host);
phase26e_result phase26e_pause(phase26e_host *host);
phase26e_result phase26e_resume(phase26e_host *host);
phase26e_result phase26e_terminate(phase26e_host *host);
phase26e_result phase26e_surface(phase26e_host *host, int available,
                                  uint32_t width, uint32_t height, uint32_t rotation);
phase26e_result phase26e_scene_add(phase26e_host *host, phase26e_id *out);
phase26e_result phase26e_window_add(phase26e_host *host, phase26e_id scene,
                                     phase26e_rect bounds, phase26e_id *out);
phase26e_result phase26e_view_add(phase26e_host *host, phase26e_id window,
                                   phase26e_id parent, phase26e_rect bounds,
                                   int32_t z, phase26e_id *out);
phase26e_result phase26e_view_remove(phase26e_host *host, phase26e_id view);
phase26e_result phase26e_hit_test(phase26e_host *host, phase26e_id window,
                                   float x, float y, phase26e_id *out);
/* Ordered receiver chain: target, superviews, window, scene. */
size_t phase26e_responder_chain(phase26e_host *host, phase26e_id view,
                                phase26e_id *out, size_t capacity);
phase26e_result phase26e_convert_to_window(phase26e_host *host,
                                            phase26e_id view, float x, float y,
                                            float *window_x, float *window_y);
phase26e_result phase26e_pointer(phase26e_host *host, phase26e_event_kind kind,
                                  int32_t pointer, float x, float y);
phase26e_result phase26e_keyboard(phase26e_host *host, int down, int32_t keycode);
phase26e_result phase26e_text(phase26e_host *host, const char *utf8, size_t length);
phase26e_result phase26e_compose(phase26e_host *host, const char *utf8, size_t length);
phase26e_result phase26e_focus(phase26e_host *host, int focused);
phase26e_result phase26e_accessibility_focus(phase26e_host *host, phase26e_id view);
phase26e_result phase26e_service(phase26e_host *host, const char *service);
phase26e_result phase26e_request_frame(phase26e_host *host);
/* Poll is an adapter boundary for a future compositor. A stale generation is rejected. */
phase26e_result phase26e_present(phase26e_host *host, uint64_t surface_generation);
size_t phase26e_drain(phase26e_host *host, phase26e_event *out, size_t capacity);
phase26e_metrics phase26e_get_metrics(phase26e_host *host);

#ifdef __cplusplus
}
#endif
#endif