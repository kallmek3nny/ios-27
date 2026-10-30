#include "ios27/phase25c_lifetime.h"
/* Separately linked image A; operates on an object created by the host. */
int phase25c_image_a_retain(ios27_phase25c_runtime *rt, void *p) {
    return ios27_phase25c_retain(rt, p);
}
int phase25c_image_a_weak(ios27_phase25c_runtime *rt, void **slot, void *p) {
    return ios27_phase25c_store_weak(rt, slot, p);
}
