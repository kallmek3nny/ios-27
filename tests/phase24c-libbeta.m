@interface Phase24cRoot
- (unsigned long)number:(unsigned long)n;
@end
@interface Phase24cChild : Phase24cRoot
- (unsigned long)number:(unsigned long)n;
@end
@implementation Phase24cChild
- (unsigned long)number:(unsigned long)n { return [super number:n] + 21u; }
@end
