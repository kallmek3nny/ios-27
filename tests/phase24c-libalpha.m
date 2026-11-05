__attribute__((objc_root_class))
@interface Phase24cRoot { @public unsigned long value; }
- (unsigned long)number:(unsigned long)n;
+ (unsigned long)kind;
@end
@implementation Phase24cRoot
- (unsigned long)number:(unsigned long)n { return value + n + 11u; }
+ (unsigned long)kind { return 1u; }
+ (void)load { }
+ (void)initialize { }
@end
