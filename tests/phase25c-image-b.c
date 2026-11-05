#include "ios27/phase25c_lifetime.h"
/* Separately linked image B shares one runtime and class/object identity. */
int phase25c_image_b_release(ios27_phase25c_runtime *rt, void *p) {
    return ios27_phase25c_release(rt, p);
}
int phase25c_image_b_load(ios27_phase25c_runtime *rt, void **slot, void **out) {
    return ios27_phase25c_load_weak_retained(rt, slot, out);
}
