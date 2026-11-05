#include "phase25d-fixtures.h"
unsigned phase25d_global_5 = 5;
extern unsigned phase25d_probe_4(void);
unsigned phase25d_probe_5(void) { return phase25d_global_5 + phase25d_probe_4(); }
@implementation P25DBase5
+ (void)load { phase25d_global_5 = phase25d_probe_5() + 1; }
+ (void)initialize { phase25d_global_5 = phase25d_probe_5() + 2; }
+ (unsigned)score { return phase25d_probe_5(); }
@end
@implementation P25DBase4 (P25DCategory5)
+ (void)load { phase25d_global_5 += 3; }
+ (unsigned)categoryScore5 { return phase25d_probe_5(); }
@end
