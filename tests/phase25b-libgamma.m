@interface P25Root
+ (unsigned long long)kind;
@end
__attribute__((objc_root_class))
@interface P25Gamma
- (unsigned long long)fromAlpha:(P25Root *)obj;
@end
@implementation P25Gamma
- (unsigned long long)fromAlpha:(P25Root *)obj {
    (void)obj;
    return [P25Root kind];
}
@end
@interface P25Root (Replacement)
- (unsigned long long)add:(unsigned long long)x;
@end
@implementation P25Root (Replacement)
- (unsigned long long)add:(unsigned long long)x { return x + 9; }
@end
