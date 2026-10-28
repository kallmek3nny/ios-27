/* Real Clang-generated Objective-C method bodies and message sends.
 * Freestanding source: no SDK, no Foundation, no Apple binaries. */
extern int phase20c_store_child(void *owner, void *child);
extern int phase20c_queue(void *object);
__attribute__((objc_root_class))
@interface Ledger {
@public
    void *isa; /* GNU-legacy fixture header; real Darwin isa is loader-owned. */
    long value;
    id child;
}
- (long)total:(unsigned)depth;
- (long)base;
- (void)setChild:(id)other;
- (void)queue;
@end
@implementation Ledger
- (long)base { return value; }
- (void)setChild:(id)other { (void)phase20c_store_child(self, other); }
- (void)queue { (void)phase20c_queue(self); }
- (long)total:(unsigned)depth {
    if (depth == 0) return [self base];
    long current = [self total:depth - 1];
    return current + 1;
}
@end
@interface Extended : Ledger
- (long)base;
@end
@implementation Extended
- (long)base { return value + 9; }
@end
/* Build code that dispatches compiler-generated messages to an inherited
 * method and an overridden method from a second independently compiled TU. */
long phase20c_compiled_graph(Ledger *left, Ledger *right) {
    [left setChild:right];
    return [left total:4] + [right total:2];
}
