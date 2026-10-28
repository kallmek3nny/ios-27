__attribute__((objc_root_class))
@interface Base { int _base; }
- (int)value;
@end
@implementation Base
- (int)value { return 11; }
@end
@protocol Printable
- (int)printValue;
@optional
- (int)optionalValue;
@end
@interface Child : Base <Printable> { int _count; }
@property (readonly) int count;
- (int)printValue;
+ (int)kind;
@end
@implementation Child
- (int)count { return _count; }
- (int)printValue { return [self value] + 2; }
+ (int)kind { return 12; }
@end
@interface Child (Extras)
- (int)extra;
@end
@implementation Child (Extras)
- (int)extra { return 77; }
@end
