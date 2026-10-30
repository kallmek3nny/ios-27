__attribute__((objc_root_class)) @interface Phase25Root
- (long)score;
@end
@interface Phase25Branch : Phase25Root
- (long)branchValue;
@end
@interface Phase25Leaf : Phase25Branch
- (long)leafValue;
@end
@implementation Phase25Leaf
- (long)leafValue { return [super branchValue] + 25; }
@end
