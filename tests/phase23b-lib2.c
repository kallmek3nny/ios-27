/* Original-source, freestanding Darwin dylib fixture: provider 1. */
extern volatile int phase23b_shared;
extern int phase23b_value1(void);
static volatile int local_generation;
__attribute__((visibility("default"))) int phase23b_value2(void) {
    return phase23b_value1() + local_generation + 2;
}
__attribute__((constructor)) static void phase23b_init2(void) {
    phase23b_shared += phase23b_value1() ? 1 : 0;
    ++local_generation;
}
static void phase23b_term2(void) {
    --phase23b_shared;
    --local_generation;
}
__attribute__((used,section("__DATA,__mod_term_func")))
static void (*const phase23b_terminator2)(void) = phase23b_term2;
