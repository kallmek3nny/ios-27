/* SDK-free Clang Objective-C 2.0 ARM64 Darwin MH_OBJECT fixture. */
__attribute__((objc_root_class)) @interface Phase23cRoot { @public unsigned long payload; }
- (unsigned long)value;
- (unsigned long)add:(unsigned long)n;
@end
@implementation Phase23cRoot
- (unsigned long)value { return 11u; }
- (unsigned long)add:(unsigned long)n { return [self value] + n; }
@end
unsigned long phase23c_root_entry(Phase23cRoot *object) {
    return [object add:5u];
}

@interface Phase23cChild : Phase23cRoot
- (unsigned long)childValue;
@end
@implementation Phase23cChild
- (unsigned long)childValue { return 31u; }
@end
