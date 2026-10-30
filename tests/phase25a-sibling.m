__attribute__((objc_root_class)) @interface Phase25Root
- (long)score;
@end
@interface Phase25Sibling : Phase25Root
- (long)siblingValue;
@end
@implementation Phase25Sibling
- (long)siblingValue { return [self score] + 1; }
@end
