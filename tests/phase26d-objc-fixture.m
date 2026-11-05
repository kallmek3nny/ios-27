/* This is intentionally a genuine compiler-produced Darwin ObjC2 object,
   NOT evidence of executing Apple's CoreAnimation framework. */
__attribute__((objc_root_class))
@interface PH26DLayer {
@public
    int _opacity;
}
- (int)opacity;
- (void)setOpacity:(int)value;
- (int)doubleOpacity;
@end
@implementation PH26DLayer
- (int)opacity { return _opacity; }
- (void)setOpacity:(int)value { _opacity = value; }
- (int)doubleOpacity { return [self opacity] * 2; }
@end
