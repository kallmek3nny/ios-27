/* SDK-free genuine Darwin ObjC2 compiler output: class, metaclass, selrefs,
 * inherited dispatch, super2, recursive and nested sends. */
__attribute__((objc_root_class))
@interface P20Root { @public void *isa; }
+ (long)classTag;
+ (void)initialize;
- (long)value:(long)x;
- (long)recursive:(long)n;
@end
@implementation P20Root
+ (long)classTag { return 40; }
+ (void)initialize { (void)[self classTag]; }
- (long)value:(long)x { return x + 2; }
- (long)recursive:(long)n {
    return n ? [self recursive:n-1] + 1 : [self value:0];
}
@end
@interface P20Child : P20Root
- (long)cross:(P20Root *)other input:(long)x;
@end
@implementation P20Child
+ (long)classTag { return [super classTag] + 1; }
- (long)value:(long)x { return [super value:x] + 100; }
- (long)cross:(P20Root *)other input:(long)x {
    return [other value:x] + [self recursive:2] + [P20Child classTag];
}
@end
long phase20b_compiled_entry(P20Root *base, P20Child *child) {
    return [base value:3] + [child cross:base input:4];
}
