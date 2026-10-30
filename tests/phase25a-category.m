__attribute__((objc_root_class)) @interface Phase25Root @end
@interface Phase25Branch : Phase25Root
- (long)branchValue;
@end
@interface Phase25Branch (Phase25Extras)
- (long)categoryValue;
- (long)score;
@end
@implementation Phase25Branch (Phase25Extras)
- (long)categoryValue { return 255; }
- (long)score { return 1255; }
@end
