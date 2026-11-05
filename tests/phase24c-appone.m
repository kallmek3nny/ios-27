@interface Phase24cRoot
- (unsigned long)number:(unsigned long)n;
@end
unsigned long phase24c_app_one(Phase24cRoot *p) { return [p number:11u]; }
int main(void) { return 0; }
