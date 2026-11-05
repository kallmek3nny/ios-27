#ifndef IOS27_PHASE27D_SCENE_H
#define IOS27_PHASE27D_SCENE_H
#include "ios27/phase26d_compositor.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define P27D_MAX_VIEWS 64u
#define P27D_MAX_TX_DEPTH 8u
#define P27D_MAX_POINTERS 10u
#define P27D_MAX_ANIMATIONS 6u

typedef struct p27d_scene p27d_scene;
typedef struct { uint32_t slot, generation; } p27d_id;
typedef enum {
    P27D_OK=0, P27D_INVALID=-1, P27D_LIMIT=-2,
    P27D_STALE=-3, P27D_STATE=-4, P27D_MEMORY=-5
} p27d_result;
typedef enum {
    P27D_LINEAR=0, P27D_EASE_IN=1, P27D_EASE_OUT=2, P27D_SMOOTH=3
} p27d_curve;
typedef enum {
    P27D_ANIM_X=1, P27D_ANIM_Y=2, P27D_ANIM_OPACITY=3,
    P27D_ANIM_SCALE=4, P27D_ANIM_TRANSFORM=5
} p27d_anim_kind;
typedef struct {
    phase26d_layer_desc layer;
    bool hidden;
    bool accepts_input;
} p27d_view_desc;
typedef enum {
    P27D_DETACHED=0, P27D_ATTACHED=1, P27D_VISIBLE=2
} p27d_window_state;
typedef enum {
    P27D_CONTROLLER_UNLOADED=0,
    P27D_CONTROLLER_LOADED=1,
    P27D_CONTROLLER_VISIBLE=2
} p27d_controller_state;
typedef struct {
    uint64_t revision, timestamp_ns;
    bool full;
} p27d_damage;
typedef enum {
    P27D_POINTER_DOWN=1, P27D_POINTER_MOVE=2,
    P27D_POINTER_UP=3, P27D_POINTER_CANCEL=4
} p27d_pointer_action;
typedef struct {
    p27d_pointer_action action;
    uint64_t timestamp_ns;
    uint32_t pointer_id;
    double x,y; /* scene pixels; finite; origin at top left */
} p27d_pointer_event;
typedef struct {
    p27d_id path[P27D_MAX_VIEWS]; /* leaf first, ancestors afterward */
    uint32_t count;
    double local_x,local_y; /* leaf-local coordinates */
    bool captured;
} p27d_event_route;
typedef struct {
    const uint8_t *pixels; /* borrowed for synchronous callback only */
    uint32_t width,height,stride;
    size_t bytes;
    uint64_t timestamp_ns,revision,fnv1a64;
    /* fixed format: premultiplied rgba8, row-major, sRGB-assumed */
} p27d_surface_frame;
typedef bool (*p27d_surface_sink)(void *userdata, const p27d_surface_frame *frame);

/* No Apple framework binaries. Caller must quiesce all users before free. */
p27d_scene *p27d_new(void);
void p27d_free(p27d_scene *scene);
p27d_view_desc p27d_default_desc(void);

/* Nested savepoints and outermost atomic model + framebuffer publication.
 * Only the transaction owner may mutate while a transaction is open. */
p27d_result p27d_begin(p27d_scene *scene);
p27d_result p27d_commit(p27d_scene *scene);
p27d_result p27d_rollback(p27d_scene *scene);
p27d_result p27d_create(p27d_scene *scene, const p27d_view_desc *desc, p27d_id *id);
p27d_result p27d_set(p27d_scene *scene, p27d_id id, const p27d_view_desc *desc);
p27d_result p27d_parent(p27d_scene *scene, p27d_id child, p27d_id parent);
p27d_result p27d_remove(p27d_scene *scene, p27d_id id);
p27d_result p27d_get(p27d_scene *scene, p27d_id id, bool presentation, p27d_view_desc *out);

/* A window has one root view and a minimal attach/visible/detach lifecycle.
 * A controller root is represented by the same stable view identity. */
p27d_result p27d_window(p27d_scene *scene, p27d_id root, p27d_window_state state);
p27d_result p27d_window_get(p27d_scene *scene, p27d_id *root, p27d_window_state *state);
/* Original controller-root lifecycle; valid transitions UNLOADED -> LOADED
 * -> VISIBLE, and VISIBLE -> LOADED -> UNLOADED. */
p27d_result p27d_controller_set(p27d_scene *scene, p27d_id view,
                                  p27d_controller_state state);
p27d_result p27d_controller_get(p27d_scene *scene, p27d_id *view,
                                  p27d_controller_state *state);

p27d_result p27d_animate(p27d_scene *scene, p27d_id id, p27d_anim_kind kind,
                          double from, double to, uint64_t start_ns, uint64_t duration_ns,
                          p27d_curve curve);
p27d_result p27d_animate_transform(p27d_scene *scene, p27d_id id,
                          phase26d_affine from, phase26d_affine to,
                          uint64_t start_ns, uint64_t duration_ns, p27d_curve curve);
p27d_result p27d_cancel(p27d_scene *scene, p27d_id id, p27d_anim_kind kind);
p27d_result p27d_step(p27d_scene *scene, uint64_t now_ns);
p27d_result p27d_next_frame(p27d_scene *scene, uint64_t now_ns,
                              uint64_t *deadline_ns, bool *needed);
p27d_result p27d_damage_since(p27d_scene *scene, uint64_t revision, p27d_damage *out);
p27d_result p27d_render(p27d_scene *scene, phase26d_frame buffer, uint64_t *fnv1a64);
p27d_result p27d_submit(p27d_scene *scene, uint32_t width, uint32_t height,
                           p27d_surface_sink sink, void *userdata);
p27d_result p27d_convert(p27d_scene *scene, p27d_id view, bool to_local,
                           double x, double y, double *out_x, double *out_y);
p27d_result p27d_hit_test(p27d_scene *scene, double x, double y, p27d_id *hit);
p27d_result p27d_route_event(p27d_scene *scene, const p27d_pointer_event *event,
                               p27d_event_route *route);
#endif
