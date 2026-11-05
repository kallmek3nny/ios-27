/* Original-source, freestanding Darwin dylib fixture: provider 0. */
extern volatile int phase23b_shared;
extern int phase23b_value0(void);
static volatile int local_generation;
__attribute__((visibility("default"))) int phase23b_value1(void) {
    return phase23b_value0() + local_generation + 1;
}
__attribute__((constructor)) static void phase23b_init1(void) {
    phase23b_shared += phase23b_value0() ? 1 : 0;
    ++local_generation;
}
static void phase23b_term1(void) {
    --phase23b_shared;
    --local_generation;
}
__attribute__((used,section("__DATA,__mod_term_func")))
static void (*const phase23b_terminator1)(void) = phase23b_term1;
