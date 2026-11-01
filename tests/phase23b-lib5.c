/* Original-source, freestanding Darwin dylib fixture: provider 4. */
extern volatile int phase23b_shared;
extern int phase23b_value4(void);
static volatile int local_generation;
__attribute__((visibility("default"))) int phase23b_value5(void) {
    return phase23b_value4() + local_generation + 5;
}
__attribute__((constructor)) static void phase23b_init5(void) {
    phase23b_shared += phase23b_value4() ? 1 : 0;
    ++local_generation;
}
static void phase23b_term5(void) {
    --phase23b_shared;
    --local_generation;
}
__attribute__((used,section("__DATA,__mod_term_func")))
static void (*const phase23b_terminator5)(void) = phase23b_term5;
