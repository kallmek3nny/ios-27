/* SDK-free, real Clang ARM64 Darwin compiler-produced external callsites.
 * The Mach-O object is NOT executable without a Darwin loader and ABI bridge. */
#include "ios27/libsystem21b_lifecycle.h"
struct scene { ios27_21b_runtime *runtime; ios27_21b_thread id; int count; };
static void *worker_call(void *arg) {
    struct scene *s = arg;
    ios27_21b_thread self = ios27_21b_thread_self(s->runtime);
    if (ios27_21b_thread_equal(self, s->id)) ++s->count;
    return arg;
}
int phase21b_darwin_integration(struct scene *scene) {
    ios27_21b_attrs attr;
    ios27_21b_info info;
    void *value = 0;
    int err = ios27_21b_attr_init(&attr);
    if (err) return err;
    attr.stack_bytes = 1048576;
    attr.guard_bytes = 4096;
    err = ios27_21b_thread_create(scene->runtime, &scene->id, &attr,
                                   worker_call, scene);
    if (err) return err;
    err = ios27_21b_thread_info(scene->runtime, scene->id, &info);
    if (err) return err;
    err = ios27_21b_thread_join(scene->runtime, scene->id, &value);
    if (err) return err;
    return value == scene ? scene->count : -1;
}
