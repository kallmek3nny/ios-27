@interface Phase24cRoot
- (unsigned long)number:(unsigned long)n;
@end
@interface Phase24cChild : Phase24cRoot
- (unsigned long)number:(unsigned long)n;
@end
@interface Phase24cLeaf : Phase24cChild
- (unsigned long)leaf:(unsigned long)n;
@end
@implementation Phase24cLeaf
- (unsigned long)leaf:(unsigned long)n { return [super number:n] + 7u; }
@end
