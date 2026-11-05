@interface P25Root
- (void *)echo:(void *)p;
@end
__attribute__((objc_root_class))
@interface P25Epsilon
- (void *)forward:(P25Root *)obj pointer:(void *)p;
@end
@implementation P25Epsilon
- (void *)forward:(P25Root *)obj pointer:(void *)p { return [obj echo:p]; }
@end
