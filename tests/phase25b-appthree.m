@interface P25Root
+ (unsigned long long)kind;
@end
@interface P25Gamma
- (unsigned long long)fromAlpha:(P25Root *)obj;
@end
int main(void) { return [P25Root kind] == 25 ? 0 : 3; }
