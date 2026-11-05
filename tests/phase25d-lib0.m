#include "phase25d-fixtures.h"
unsigned phase25d_global_0 = 0;
unsigned phase25d_probe_0(void) { return phase25d_global_0; }
@implementation P25DBase0
+ (void)load { phase25d_global_0 = phase25d_probe_0() + 1; }
+ (void)initialize { phase25d_global_0 = phase25d_probe_0() + 2; }
+ (unsigned)score { return phase25d_probe_0(); }
@end
