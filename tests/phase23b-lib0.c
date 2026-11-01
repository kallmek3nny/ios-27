/* Original-source, freestanding Darwin dylib fixture: no Apple SDK. */
__attribute__((visibility("default"))) volatile int phase23b_shared;
static volatile int local_generation;
__attribute__((visibility("default"))) int phase23b_value0(void) { return 11 + local_generation; }
__attribute__((constructor)) static void phase23b_init0(void) { phase23b_shared += 1; local_generation += 1; }
static void phase23b_term0(void) { phase23b_shared -= 1; local_generation -= 1; }
__attribute__((used,section("__DATA,__mod_term_func")))
static void (*const phase23b_terminator0)(void) = phase23b_term0;
