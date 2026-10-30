__attribute__((objc_root_class))
@interface P25Root
+ (unsigned long long)kind;
- (unsigned long long)add:(unsigned long long)x;
- (double)twice:(double)x;
- (float)triple:(float)x;
- (void *)echo:(void *)p;
- (unsigned long long)six:(unsigned long long)a b:(unsigned long long)b
    c:(unsigned long long)c d:(unsigned long long)d
    e:(unsigned long long)e f:(unsigned long long)f;
@end
@implementation P25Root
+ (unsigned long long)kind { return 25; }
- (unsigned long long)add:(unsigned long long)x { return x + 5; }
- (double)twice:(double)x { return x + x; }
- (float)triple:(float)x { return x + x + x; }
- (void *)echo:(void *)p { return p; }
- (unsigned long long)six:(unsigned long long)a b:(unsigned long long)b
    c:(unsigned long long)c d:(unsigned long long)d
    e:(unsigned long long)e f:(unsigned long long)f {
    (void)b; (void)c; (void)d; (void)e;
    return a + f;
}
@end
