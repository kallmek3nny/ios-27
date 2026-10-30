__attribute__((objc_root_class)) @interface Phase25Root @end
@interface Phase25Root (Phase25Overrides)
- (long)overrideValue;
- (long)score;
@end
@implementation Phase25Root (Phase25Overrides)
- (long)overrideValue { return [self score] + 8; }
- (long)score { return 1011; }
@end
