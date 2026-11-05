__attribute__((objc_root_class)) @interface Phase23cCategoryRoot
- (unsigned long)value;
@end
@implementation Phase23cCategoryRoot
- (unsigned long)value { return 1u; }
@end
@interface Phase23cCategoryRoot (Extra)
- (unsigned long)extra;
@end
@implementation Phase23cCategoryRoot (Extra)
- (unsigned long)extra { return 2u; }
@end
