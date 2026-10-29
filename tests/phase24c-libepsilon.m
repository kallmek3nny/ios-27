@interface Phase24cRoot
- (unsigned long)number:(unsigned long)n;
@end
@interface Phase24cRoot (Late)
- (unsigned long)late:(unsigned long)n;
@end
@implementation Phase24cRoot (Late)
- (unsigned long)late:(unsigned long)n { return [self number:n] + 9u; }
+ (void)load { }
@end
unsigned long phase24c_epsilon_anchor(unsigned long n) { return n + 5u; }
