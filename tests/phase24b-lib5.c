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
static void phase24b_dtor5(void) {
    phase24b_term_seen5 += phase24b_global5;
}
int phase24b_value5(void) {
    return (int)phase24b_global5 + (int)phase24b_init_seen5;
}
__attribute__((used, section("__DATA,__mod_term_func")))
static void (*const phase24b_term_entry5)(void) = phase24b_dtor5;
