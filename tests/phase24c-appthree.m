@interface Phase24cRoot
- (unsigned long)number:(unsigned long)n;
- (unsigned long)work:(unsigned long)n;
@end
extern unsigned long phase24c_gamma_anchor(unsigned long n);
unsigned long phase24c_app_three(Phase24cRoot *p) {
    return [p number:2u] + [p work:3u] + phase24c_gamma_anchor(1u);
}
int main(void) { return 0; }
