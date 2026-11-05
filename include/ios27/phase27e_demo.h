#ifndef IOS27_PHASE27E_DEMO_H
#define IOS27_PHASE27E_DEMO_H
/* Temporary original software scene. All output is RGBA8888, straight alpha 255. */
#include <stddef.h>
#include <stdint.h>
#define P27_MAX_PIXELS 2097152u
#define P27_MAX_POINTERS 16u

typedef struct {
    int32_t id;
    float x, y;
    int active;
} p27_pointer;
typedef struct {
    p27_pointer pointers[P27_MAX_POINTERS];
    float last_x, last_y;
    uint64_t last_input_ms;
    uint32_t tap_count;
    uint32_t input_count;
} p27_scene;

void p27_scene_init(p27_scene *scene);
/* Kind: 1 down, 2 move, 3 up, 4 cancel. Coordinates normalized to [0,1]. */
int p27_scene_pointer(p27_scene *scene, int kind, int32_t id,
                      float nx, float ny, uint64_t now_ms);
int p27_scene_render(const p27_scene *scene, uint8_t *rgba, size_t capacity,
                     uint32_t width, uint32_t height, uint32_t stride,
                     uint64_t now_ms);
uint64_t p27_frame_hash(const uint8_t *rgba, size_t bytes);
#endif
