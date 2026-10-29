/* Original SDK-free Clang arm64 Darwin test dylib 4. No Apple frameworks. */
extern int phase24b_value3(void);
extern int phase24b_value2(void);
extern volatile int phase24b_global2;
volatile int phase24b_global4 = 11;
volatile int phase24b_init_seen4 = 0;
volatile int phase24b_term_seen4 = 0;
__attribute__((constructor)) static void phase24b_ctor4(void) {
    phase24b_init_seen4 += 5;
    phase24b_global4 += phase24b_value3() + phase24b_value2() + phase24b_global2;
}
static void phase24b_dtor4(void) {
    phase24b_term_seen4 += phase24b_global4;
}
int phase24b_value4(void) {
    return (int)phase24b_global4 + (int)phase24b_init_seen4;
}
__attribute__((used, section("__DATA,__mod_term_func")))
static void (*const phase24b_term_entry4)(void) = phase24b_dtor4;
