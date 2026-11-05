@interface Phase24cRoot
- (unsigned long)number:(unsigned long)n;
@end
@interface Phase24cChild : Phase24cRoot
- (unsigned long)number:(unsigned long)n;
@end
unsigned long phase24c_app_two(Phase24cChild *p) { return [p number:12u]; }
int main(void) { return 0; }
