/* This TU is genuinely compiled with Objective-C ARC for the host target.
 * Its calls are not handwritten C calls to objc_retain/weak/pools. */
typedef struct objc_object *id;
extern void phase25c_consume(id);
__attribute__((visibility("default"), noinline))
void phase25c_compiler_arc(id object) {
    id strong = object;
    __weak id weak = object;
    @autoreleasepool {
        id loaded = weak;
        phase25c_consume(strong);
        phase25c_consume(loaded);
    }
}
