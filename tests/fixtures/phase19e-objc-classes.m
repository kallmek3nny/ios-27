__attribute__((objc_root_class))
@interface Root
{ @public long value; }
- (long)number;
- (long)answer;
- (id)init;
@end
@implementation Root
- (id)init { value = 37; return self; }
- (long)number { return value; }
- (long)answer { return 42; }
@end
@interface Child : Root
- (long)number;
@end
@implementation Child
- (long)number { return value + 5; }
@end
