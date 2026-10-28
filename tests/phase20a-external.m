@interface Base
- (int)value;
@end
@interface Grand : Base
- (int)grand;
@end
@implementation Grand
- (int)grand { return 39; }
@end
