__attribute__((objc_root_class)) @interface Phase25Root
- (long)score;
@end
@interface Phase25Root (Phase25Overrides)
- (long)overrideValue;
@end
@implementation Phase25Root (Phase25Overrides)
- (long)overrideValue { return [self score] + 8; }
@end
