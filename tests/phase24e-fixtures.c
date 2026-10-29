/* Original-source SDK-free ARM64 Darwin test images.
 * Compile independently with one PHASE24E_* definition per image.
 * No host symbols, Apple frameworks, TLS, or unverified guest syscalls. */
#include <stdint.h>

#if defined(PHASE24E_BASE)
__attribute__((visibility("default"))) volatile int phase24e_base_value = 7;
__attribute__((visibility("default"), noinline))
int phase24e_base_inc(int n) { return n + phase24e_base_value; }
#elif defined(PHASE24E_MATH)
extern volatile int phase24e_base_value;
extern int phase24e_base_inc(int);
__attribute__((visibility("default"), noinline))
int phase24e_math_eval(int n) {
    return phase24e_base_inc(n) + phase24e_base_value;
}
#elif defined(PHASE24E_LOGIC)
extern int phase24e_math_eval(int);
__attribute__((visibility("default"), noinline))
int phase24e_logic_eval(int n) { return phase24e_math_eval(n) * 2; }
#elif defined(PHASE24E_STATE)
extern volatile int phase24e_base_value;
__attribute__((visibility("default"))) volatile int phase24e_state_counter = 1;
__attribute__((visibility("default"), noinline))
int phase24e_state_next(int step) {
    phase24e_state_counter += step;
    return phase24e_state_counter + phase24e_base_value;
}
#elif defined(PHASE24E_PIPELINE)
extern int phase24e_logic_eval(int);
extern int phase24e_state_next(int);
__attribute__((visibility("default"), noinline))
int phase24e_pipeline_eval(int n) {
    return phase24e_logic_eval(n) + phase24e_state_next(1);
}
#elif defined(PHASE24E_CTOR)
__attribute__((visibility("default"))) volatile int phase24e_ctor_state;
__attribute__((constructor))
static void phase24e_constructor(void) { phase24e_ctor_state = 91; }
static void phase24e_destructor(void) { phase24e_ctor_state = -1; }
__attribute__((used, section("__DATA,__mod_term_func")))
static void (*const phase24e_term_callback)(void) = phase24e_destructor;
__attribute__((visibility("default"), noinline))
int phase24e_ctor_value(void) { return phase24e_ctor_state; }
#elif defined(PHASE24E_APP_CALC) || defined(PHASE24E_APP_STATE) || defined(PHASE24E_APP_PIPELINE)
extern volatile int phase24e_base_value;
extern volatile int phase24e_state_counter;
extern int phase24e_base_inc(int);
extern int phase24e_math_eval(int);
extern int phase24e_logic_eval(int);
extern int phase24e_state_next(int);
extern int phase24e_pipeline_eval(int);
__attribute__((visibility("default"))) volatile uint64_t phase24e_guest_operations;

static int equal(const char *a, const char *b) {
    unsigned n = 0u;
    if (!a || !b) return 0;
    while (n < 128u && a[n] && a[n] == b[n]) ++n;
    return n < 128u && a[n] == b[n];
}
__attribute__((visibility("default"), used, noinline))
int main(int argc, char **argv, char **envp, char **apple) {
    uintptr_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    if ((sp & 15u) || argc != 2 || !argv || !envp || !apple ||
        !equal(argv[0], "phase24e") || !equal(envp[0], "MODE=trusted") ||
        !equal(apple[0], "exec=/App/phase24e") ||
        argv[2] || envp[1] || apple[1]) return 20;
#if defined(PHASE24E_APP_CALC)
    if (!equal(argv[1], "calc") || phase24e_base_value != 7) return 21;
    for (int i = 0; i < 1024; ++i) {
        if (phase24e_base_inc(i) != i + 7) return 22;
        ++phase24e_guest_operations;
        if (phase24e_math_eval(i) != i + 14) return 23;
        ++phase24e_guest_operations;
        if (phase24e_logic_eval(i) != i * 2 + 28) return 24;
        ++phase24e_guest_operations;
    }
#elif defined(PHASE24E_APP_STATE)
    if (!equal(argv[1], "state") || phase24e_base_value != 7) return 25;
    for (int i = 0; i < 1024; ++i) {
        if (phase24e_state_next(1) != i + 9) return 26;
        ++phase24e_guest_operations;
    }
    if (phase24e_state_counter != 1025) return 27;
#else
    if (!equal(argv[1], "pipeline")) return 28;
    for (int i = 0; i < 1024; ++i) {
        if (phase24e_pipeline_eval(3) != i + 43) return 29;
        ++phase24e_guest_operations;
    }
    if (phase24e_state_counter != 1025) return 30;
#endif
    /* Exercise private guest argument/environment/apple-string copies. */
    argv[1][0] = 'x';
    envp[0][0] = 'x';
    apple[0][0] = 'x';
    return 0;
}
#else
#error "compile exactly one PHASE24E_* image mode"
#endif
