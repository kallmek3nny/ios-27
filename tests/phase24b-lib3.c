/* Original SDK-free Clang arm64 Darwin test dylib 3. No Apple frameworks. */
extern int phase24b_value2(void);
extern volatile int phase24b_global2;
volatile int phase24b_global3 = 10;
volatile int phase24b_init_seen3 = 0;
volatile int phase24b_term_seen3 = 0;
__attribute__((constructor)) static void phase24b_ctor3(void) {
    phase24b_init_seen3 += 4;
    phase24b_global3 += phase24b_value2() + phase24b_global2;
}
__attribute__((destructor)) static void phase24b_dtor3(void) {
    phase24b_term_seen3 += phase24b_global3;
}
int phase24b_value3(void) {
    return (int)phase24b_global3 + (int)phase24b_init_seen3;
}
