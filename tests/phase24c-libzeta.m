__attribute__((objc_root_class))
@interface Phase24cHolder { @public id object; }
- (unsigned long)call:(unsigned long)n;
@end
@interface Phase24cRoot
- (unsigned long)number:(unsigned long)n;
@end
@implementation Phase24cHolder
- (unsigned long)call:(unsigned long)n { return [(Phase24cRoot *)object number:n]; }
@end
