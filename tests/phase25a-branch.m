__attribute__((objc_root_class)) @interface Phase25Root
- (long)score;
@end
@interface Phase25Branch : Phase25Root { @public long _extra; }
- (long)score;
- (long)branchValue;
@end
@implementation Phase25Branch
- (long)score { return 45; }
- (long)branchValue { return [super score] + _extra; }
@end
