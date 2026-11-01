/* Phase25e: original-source, SDK-free Objective-C 2.0 Darwin fixtures.
 * All translation units are compiled and linked independently.
 * Neither the app counter nor the host builder proves guest execution. */
#include <stdint.h>
typedef struct objc_class *Phase25eClass;
extern id objc_alloc(Phase25eClass);
extern id objc_retain(id);
extern void objc_release(id);

__attribute__((objc_root_class))
@interface Phase25eRoot {
@public
    int _value;
}
+ (id)alloc;
+ (int)seed;
- (id)setValue:(int)n;
- (int)value;
@end
@interface Phase25eDerived : Phase25eRoot
- (int)value;
@end
@protocol Phase25eMeasure
- (int)measure:(int)n;
@end
@interface Phase25eWorker : Phase25eRoot <Phase25eMeasure>
- (int)measure:(int)n;
@end
@interface Phase25eLedger : Phase25eRoot
+ (int)advance:(int)n;
@end
@interface Phase25ePipeline : Phase25eRoot
+ (int)calculate:(int)n;
@end
@interface Phase25eRoot (Phase25eExtras)
- (int)categoryValue;
@end

#if defined(PHASE25E_ROOT)
__attribute__((visibility("default"))) volatile int phase25e_load_root;
__attribute__((visibility("default"))) volatile int phase25e_init_root;
@implementation Phase25eRoot
+ (id)alloc { return objc_alloc((Phase25eClass)self); }
+ (int)seed { return 7; }
- (id)setValue:(int)n { _value = n; return self; }
- (int)value { return _value + 7; }
+ (void)load { phase25e_load_root = 1; }
+ (void)initialize { ++phase25e_init_root; }
@end
#elif defined(PHASE25E_DERIVED)
extern volatile int phase25e_load_root;
__attribute__((visibility("default"))) volatile int phase25e_load_derived;
@implementation Phase25eDerived
- (int)value { return [super value] + 3; }
+ (void)load { phase25e_load_derived = phase25e_load_root + 1; }
+ (void)initialize { }
@end
#elif defined(PHASE25E_WORKER)
@implementation Phase25eWorker
- (int)measure:(int)n { return [self value] + n * 2; }
@end
#elif defined(PHASE25E_CATEGORY)
@implementation Phase25eRoot (Phase25eExtras)
- (int)categoryValue { return [self value] + 11; }
+ (void)load { }
@end
#elif defined(PHASE25E_LEDGER)
__attribute__((visibility("default"))) volatile int phase25e_ledger_total;
@implementation Phase25eLedger
+ (int)advance:(int)n { phase25e_ledger_total += n; return phase25e_ledger_total; }
@end
#elif defined(PHASE25E_PIPELINE)
@implementation Phase25ePipeline
+ (int)calculate:(int)n {
    id d = [Phase25eDerived alloc];
    (void)[d setValue:n];
    int value = [d value] + [d categoryValue];
    objc_release(d);
    id w = [Phase25eWorker alloc];
    (void)[w setValue:n];
    int measured = [w measure:2];
    objc_release(w);
    return value + measured;
}
@end
#elif defined(PHASE25E_APP_BASIC) || defined(PHASE25E_APP_PROTOCOL) || defined(PHASE25E_APP_LIFECYCLE)
__attribute__((visibility("default"), used)) volatile uint64_t phase25e_guest_sends;
__attribute__((visibility("default"), used)) volatile uint64_t phase25e_guest_ownership;
extern volatile int phase25e_load_root;
extern volatile int phase25e_load_derived;
extern volatile int phase25e_init_root;
extern volatile int phase25e_ledger_total;
static int same(const char *p, const char *want) {
    if (!p || !want) return 0;
    while (*p && *want && *p == *want) { ++p; ++want; }
    return *p == *want;
}
__attribute__((visibility("default"), used))
int main(int argc, char **argv, char **env, char **apple) {
    if (argc != 2 || !argv || !argv[0] || !argv[1] || !env || !env[0] ||
        !apple || !apple[0] || !same(env[0], "MODE=phase25e") ||
        !same(apple[0], "exec=/App/phase25e")) return 41;
    if (phase25e_load_root != 1 || phase25e_load_derived != 2) return 42;
#if defined(PHASE25E_APP_BASIC)
    if (!same(argv[1], "basic")) return 43;
#elif defined(PHASE25E_APP_PROTOCOL)
    if (!same(argv[1], "protocol")) return 43;
#else
    if (!same(argv[1], "lifecycle")) return 43;
#endif
    for (int i = 0; i < 50000; ++i) {
        id d = [Phase25eDerived alloc];
        ++phase25e_guest_sends;
        (void)[d setValue:1];
        ++phase25e_guest_sends;
        if ([d value] != 11) return 44;
        ++phase25e_guest_sends;
        if ([d categoryValue] != 22) return 45;
        ++phase25e_guest_sends;
        id second = objc_retain(d);
        ++phase25e_guest_ownership;
        objc_release(second);
        ++phase25e_guest_ownership;
        objc_release(d);
        ++phase25e_guest_ownership;
    }
    if (phase25e_init_root != 1) return 46;
#if defined(PHASE25E_APP_PROTOCOL)
    if ([Phase25ePipeline calculate:3] != 51) return 47;
#elif defined(PHASE25E_APP_LIFECYCLE)
    if ([Phase25eLedger advance:2] != 2 || phase25e_ledger_total != 2)
        return 48;
#else
    if ([Phase25eRoot seed] != 7) return 49;
#endif
    return 0;
}
#else
#error "exactly one PHASE25E_* selector must be defined"
#endif
