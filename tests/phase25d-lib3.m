#include "phase25d-fixtures.h"
unsigned phase25d_global_3 = 3;
extern unsigned phase25d_probe_2(void);
unsigned phase25d_probe_3(void) { return phase25d_global_3 + phase25d_probe_2(); }
@implementation P25DBase3
+ (void)load { phase25d_global_3 = phase25d_probe_3() + 1; }
+ (void)initialize { phase25d_global_3 = phase25d_probe_3() + 2; }
+ (unsigned)score { return phase25d_probe_3(); }
@end
@implementation P25DBase2 (P25DCategory3)
+ (void)load { phase25d_global_3 += 3; }
+ (unsigned)categoryScore3 { return phase25d_probe_3(); }
@end
