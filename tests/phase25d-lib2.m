#include "phase25d-fixtures.h"
unsigned phase25d_global_2 = 2;
extern unsigned phase25d_probe_1(void);
unsigned phase25d_probe_2(void) { return phase25d_global_2 + phase25d_probe_1(); }
@implementation P25DBase2
+ (void)load { phase25d_global_2 = phase25d_probe_2() + 1; }
+ (void)initialize { phase25d_global_2 = phase25d_probe_2() + 2; }
+ (unsigned)score { return phase25d_probe_2(); }
@end
@implementation P25DBase1 (P25DCategory2)
+ (void)load { phase25d_global_2 += 3; }
+ (unsigned)categoryScore2 { return phase25d_probe_2(); }
@end
