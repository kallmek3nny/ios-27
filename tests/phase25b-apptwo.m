@interface P25Root
+ (unsigned long long)kind;
@end
@interface P25Child : P25Root
- (unsigned long long)childLeaf:(unsigned long long)x;
@end
int main(void) { return [P25Root kind] == 25 ? 0 : 2; }
