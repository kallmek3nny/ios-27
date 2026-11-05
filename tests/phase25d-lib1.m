#include "phase25d-fixtures.h"
unsigned phase25d_global_1 = 1;
extern unsigned phase25d_probe_0(void);
unsigned phase25d_probe_1(void) { return phase25d_global_1 + phase25d_probe_0(); }
@implementation P25DBase1
+ (void)load { phase25d_global_1 = phase25d_probe_1() + 1; }
+ (void)initialize { phase25d_global_1 = phase25d_probe_1() + 2; }
+ (unsigned)score { return phase25d_probe_1(); }
@end
@implementation P25DBase0 (P25DCategory1)
+ (void)load { phase25d_global_1 += 3; }
+ (unsigned)categoryScore1 { return phase25d_probe_1(); }
@end
