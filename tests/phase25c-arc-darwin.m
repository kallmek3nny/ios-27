/* Genuine SDK-free Clang ARM64 Darwin Objective-C 2.0 + ARC fixture.
 * Compiles to Mach-O and records unresolved ABI imports; it is NOT an
 * executed Darwin guest until the phase25a/25b image binder is connected. */
__attribute__((objc_root_class))
@interface Phase25CBox {
@public
    id _strong;
    __weak id _weak;
}
- (void)replace:(id)value;
- (id)readWeak;
@end

@implementation Phase25CBox
- (void)replace:(id)value {
    _strong = value;
    _weak = value;
}
- (id)readWeak { return _weak; }
- (void)dealloc { _strong = (id)0; }
@end

id phase25c_arc_entry(Phase25CBox *box, id value) {
    @autoreleasepool {
        [box replace:value];
        id strong = box->_strong;
        id weak = box->_weak;
        return weak ? weak : strong;
    }
}
