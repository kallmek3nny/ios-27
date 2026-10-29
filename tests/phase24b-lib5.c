/* Original SDK-free Clang arm64 Darwin test dylib 5. No Apple frameworks. */
extern int phase24b_value4(void);
extern volatile int phase24b_global4;
volatile int phase24b_global5 = 12;
volatile int phase24b_init_seen5 = 0;
volatile int phase24b_term_seen5 = 0;
__attribute__((constructor)) static void phase24b_ctor5(void) {
    phase24b_init_seen5 += 6;
    phase24b_global5 += phase24b_value4() + phase24b_global4;
}
__attribute__((destructor)) static void phase24b_dtor5(void) {
    phase24b_term_seen5 += phase24b_global5;
}
int phase24b_value5(void) {
    return (int)phase24b_global5 + (int)phase24b_init_seen5;
}
