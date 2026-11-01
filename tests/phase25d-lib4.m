#include "phase25d-fixtures.h"
unsigned phase25d_global_4 = 4;
extern unsigned phase25d_probe_3(void);
unsigned phase25d_probe_4(void) { return phase25d_global_4 + phase25d_probe_3(); }
@implementation P25DBase4
+ (void)load { phase25d_global_4 = phase25d_probe_4() + 1; }
+ (void)initialize { phase25d_global_4 = phase25d_probe_4() + 2; }
+ (unsigned)score { return phase25d_probe_4(); }
@end
@implementation P25DBase3 (P25DCategory4)
+ (void)load { phase25d_global_4 += 3; }
+ (unsigned)categoryScore4 { return phase25d_probe_4(); }
@end
