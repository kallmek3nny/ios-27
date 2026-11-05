@interface P25Root
- (unsigned long long)add:(unsigned long long)x;
@end
@interface P25Child : P25Root
- (unsigned long long)add:(unsigned long long)x;
@end
@interface P25Grandchild : P25Child
- (unsigned long long)add:(unsigned long long)x;
@end
@implementation P25Grandchild
- (unsigned long long)add:(unsigned long long)x { return [super add:x] + 100; }
@end
