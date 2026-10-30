@interface P25Root
+ (unsigned long long)kind;
@end
int main(void) { return [P25Root kind] == 25 ? 0 : 1; }
