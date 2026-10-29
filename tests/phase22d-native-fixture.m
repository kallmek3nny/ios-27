/* SDK-free Objective-C 2.0 compiler input. This is genuine ARM64 Darwin
 * Mach-O MH_OBJECT code and metadata, never run as host x86_64 code. */
__attribute__((objc_root_class))
@interface Phase22dRoot
{ @public unsigned long payload; }
+ (unsigned long)ping;
- (unsigned long)value;
@end
@implementation Phase22dRoot
+ (unsigned long)ping { return 91; }
- (unsigned long)value { return payload + 11; }
@end

@interface Phase22dChild : Phase22dRoot
- (unsigned long)value;
- (unsigned long)over:(unsigned long)number;
- (unsigned long)superValue;
@end
@implementation Phase22dChild
- (unsigned long)value { return 22; }
- (unsigned long)over:(unsigned long)number {
    return [self value] + number;
}
- (unsigned long)superValue { return [super value]; }
@end

@interface Phase22dChild (Extra)
- (unsigned long)categoryValue;
@end
@implementation Phase22dChild (Extra)
- (unsigned long)categoryValue { return 77; }
@end

unsigned long phase22d_compiled_entry(Phase22dChild *obj) {
    return [obj over:12] + [obj superValue] + [obj categoryValue]
         + [Phase22dRoot ping];
}
