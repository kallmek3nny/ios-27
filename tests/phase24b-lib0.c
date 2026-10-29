/* Original SDK-free Clang arm64 Darwin test dylib 0. No Apple frameworks. */
volatile int phase24b_global0 = 7;
volatile int phase24b_init_seen0 = 0;
volatile int phase24b_term_seen0 = 0;
__attribute__((constructor)) static void phase24b_ctor0(void) {
    phase24b_init_seen0 += 1;
    phase24b_global0 += 11;
}
static void phase24b_dtor0(void) {
    phase24b_term_seen0 += phase24b_global0;
}
int phase24b_value0(void) {
    return (int)phase24b_global0 + (int)phase24b_init_seen0;
}
__attribute__((used, section("__DATA,__mod_term_func")))
static void (*const phase24b_term_entry0)(void) = phase24b_dtor0;
