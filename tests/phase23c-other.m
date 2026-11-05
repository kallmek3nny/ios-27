__attribute__((objc_root_class)) @interface Phase23cOther { @public unsigned long payload; }
- (unsigned long)other;
@end
@implementation Phase23cOther
- (unsigned long)other { return 42u; }
@end
unsigned long phase23c_other_entry(Phase23cOther *object) {
    return [object other];
}

@interface Phase23cOtherChild : Phase23cOther
- (unsigned long)otherExtra;
@end
@implementation Phase23cOtherChild
- (unsigned long)otherExtra { return 52u; }
@end
