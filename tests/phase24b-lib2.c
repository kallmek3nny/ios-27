/* Original SDK-free Clang arm64 Darwin test dylib 2. No Apple frameworks. */
extern int phase24b_value1(void);
extern volatile int phase24b_global1;
volatile int phase24b_global2 = 9;
volatile int phase24b_init_seen2 = 0;
volatile int phase24b_term_seen2 = 0;
__attribute__((constructor)) static void phase24b_ctor2(void) {
    phase24b_init_seen2 += 3;
    phase24b_global2 += phase24b_value1() + phase24b_global1;
}
static void phase24b_dtor2(void) {
    phase24b_term_seen2 += phase24b_global2;
}
int phase24b_value2(void) {
    return (int)phase24b_global2 + (int)phase24b_init_seen2;
}
__attribute__((used, section("__DATA,__mod_term_func")))
static void (*const phase24b_term_entry2)(void) = phase24b_dtor2;
