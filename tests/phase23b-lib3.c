/* Original-source, freestanding Darwin dylib fixture: provider 2. */
extern volatile int phase23b_shared;
extern int phase23b_value2(void);
static volatile int local_generation;
__attribute__((visibility("default"))) int phase23b_value3(void) {
    return phase23b_value2() + local_generation + 3;
}
__attribute__((constructor)) static void phase23b_init3(void) {
    phase23b_shared += phase23b_value2() ? 1 : 0;
    ++local_generation;
}
static void phase23b_term3(void) {
    --phase23b_shared;
    --local_generation;
}
__attribute__((used,section("__DATA,__mod_term_func")))
static void (*const phase23b_terminator3)(void) = phase23b_term3;
