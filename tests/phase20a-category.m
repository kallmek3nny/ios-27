@interface Grand
- (int)grand;
@end
@interface Grand (Additional)
- (int)more;
@end
@implementation Grand (Additional)
- (int)more { return 44; }
@end
