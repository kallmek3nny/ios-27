@interface P25Root
- (unsigned long long)add:(unsigned long long)x;
@end
@interface P25Child : P25Root
- (unsigned long long)add:(unsigned long long)x;
- (unsigned long long)childLeaf:(unsigned long long)x;
@end
@implementation P25Child
- (unsigned long long)add:(unsigned long long)x {
    return [super add:x] + 20;
}
- (unsigned long long)childLeaf:(unsigned long long)x { return x + 20; }
@end
