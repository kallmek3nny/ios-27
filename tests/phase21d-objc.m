#include "ios27/libsystem21d_rw_once.h"

/* An actual Clang-compiled ARM64 Darwin Objective-C class that calls the
   original bridge through its methods. No Apple SDK or runtime is linked. */
__attribute__((objc_root_class))
@interface IOS27Phase21DStore {
    ios27_21d_runtime *_runtime;
    ios27_21d_handle _rw;
    ios27_21d_handle _once;
}
- (int)configure:(ios27_21d_runtime *)rt;
- (int)initializeWith:(ios27_21d_initializer)callback context:(void *)context;
- (int)increment:(int *)value;
- (int)read:(int *)destination;
- (int)dispose;
@end

@implementation IOS27Phase21DStore
- (int)configure:(ios27_21d_runtime *)rt {
    _runtime = rt;
    int rc = ios27_21d_create(_runtime, IOS27_21D_RWLOCK, &_rw);
    if (rc) return rc;
    rc = ios27_21d_create(_runtime, IOS27_21D_ONCE, &_once);
    if (rc) (void)ios27_21d_destroy(_runtime, _rw);
    return rc;
}
- (int)initializeWith:(ios27_21d_initializer)callback context:(void *)context {
    return ios27_21d_once(_runtime, _once, callback, context);
}
- (int)increment:(int *)value {
    int rc = ios27_21d_wrlock(_runtime, _rw);
    if (rc) return rc;
    ++*value;
    return ios27_21d_unlock(_runtime, _rw);
}
- (int)read:(int *)destination {
    int rc = ios27_21d_rdlock(_runtime, _rw);
    if (rc) return rc;
    *destination += 1;
    return ios27_21d_unlock(_runtime, _rw);
}
- (int)dispose {
    int rc = ios27_21d_destroy(_runtime, _once);
    if (rc) return rc;
    return ios27_21d_destroy(_runtime, _rw);
}
@end
