/* Actual clang-emitted Objective-C 2 metadata, no system SDK. */
@protocol Phase25Protocol
- (long)score;
@end
__attribute__((objc_root_class))
@interface Phase25Root <Phase25Protocol> { @public long _value; }
+ (long)identity;
- (long)score;
- (long)echo:(long)value;
@end
@implementation Phase25Root
+ (long)identity { return 251; }
- (long)score { return _value + 7; }
- (long)echo:(long)value { return value + _value; }
@end
