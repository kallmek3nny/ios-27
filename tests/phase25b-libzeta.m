@interface P25Root
- (unsigned long long)add:(unsigned long long)x;
@end
@interface P25Child : P25Root
- (unsigned long long)add:(unsigned long long)x;
@end
__attribute__((objc_root_class))
@interface P25Zeta
- (unsigned long long)callChild:(P25Child *)obj;
@end
@implementation P25Zeta
- (unsigned long long)callChild:(P25Child *)obj { return [obj add:4]; }
@end
