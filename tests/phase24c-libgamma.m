@protocol Phase24cWork
- (unsigned long)work:(unsigned long)n;
@end
@interface Phase24cRoot
- (unsigned long)number:(unsigned long)n;
@end
@interface Phase24cRoot (Work) <Phase24cWork>
- (unsigned long)work:(unsigned long)n;
@end
@implementation Phase24cRoot (Work)
- (unsigned long)work:(unsigned long)n { return [self number:n] + 2u; }
@end
