@interface Phase24cRoot
- (unsigned long)number:(unsigned long)n;
- (unsigned long)work:(unsigned long)n;
@end
unsigned long phase24c_app_three(Phase24cRoot *p) {
    return [p number:2u] + [p work:3u];
}
int main(void) { return 0; }
