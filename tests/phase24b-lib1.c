/* Original SDK-free Clang arm64 Darwin test dylib 1. No Apple frameworks. */
extern int phase24b_value0(void);
extern volatile int phase24b_global0;
volatile int phase24b_global1 = 8;
volatile int phase24b_init_seen1 = 0;
volatile int phase24b_term_seen1 = 0;
__attribute__((constructor)) static void phase24b_ctor1(void) {
    phase24b_init_seen1 += 2;
    phase24b_global1 += phase24b_value0() + phase24b_global0;
}
__attribute__((destructor)) static void phase24b_dtor1(void) {
    phase24b_term_seen1 += phase24b_global1;
}
int phase24b_value1(void) {
    return (int)phase24b_global1 + (int)phase24b_init_seen1;
}
