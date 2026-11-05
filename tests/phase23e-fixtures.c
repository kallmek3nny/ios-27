/* Original-source, SDK-free arm64-apple-darwin app and dylib workloads.
 * Compile once per PHASE23E_* target. No Apple headers or host calls.
 * Exactly three MH_EXECUTEs and six MH_DYLIBs, including a negative
 * constructor fixture that must never be silently initialized.
 */
#if defined(PHASE23E_BASE)
__attribute__((visibility("default"))) volatile int phase23e_base_value = 7;
__attribute__((visibility("default"),noinline))
int phase23e_base_inc(int n) { return n + phase23e_base_value; }
#elif defined(PHASE23E_MATH)
extern volatile int phase23e_base_value;
extern int phase23e_base_inc(int);
__attribute__((visibility("default"),noinline))
int phase23e_math_eval(int n) {
    return phase23e_base_inc(n) + phase23e_base_value;
}
#elif defined(PHASE23E_LOGIC)
extern int phase23e_math_eval(int);
__attribute__((visibility("default"),noinline))
int phase23e_logic_eval(int n) { return phase23e_math_eval(n) * 2; }
#elif defined(PHASE23E_STATE)
extern volatile int phase23e_base_value;
__attribute__((visibility("default"))) volatile int phase23e_state_counter = 1;
__attribute__((visibility("default"),noinline))
int phase23e_state_next(int step) {
    phase23e_state_counter += step;
    return phase23e_state_counter + phase23e_base_value;
}
#elif defined(PHASE23E_PIPELINE)
extern int phase23e_logic_eval(int);
extern int phase23e_state_next(int);
__attribute__((visibility("default"),noinline))
int phase23e_pipeline_eval(int n) {
    return phase23e_logic_eval(n) + phase23e_state_next(1);
}
#elif defined(PHASE23E_CTOR)
__attribute__((visibility("default"))) volatile int phase23e_ctor_state;
__attribute__((constructor))
static void phase23e_ctor_boot(void) { phase23e_ctor_state = 99; }
__attribute__((visibility("default")))
int phase23e_ctor_read(void) { return phase23e_ctor_state; }
#elif defined(PHASE23E_APP_CALC) || defined(PHASE23E_APP_STATE) || defined(PHASE23E_APP_PIPELINE)
extern int phase23e_base_inc(int);
extern volatile int phase23e_base_value;
extern int phase23e_math_eval(int);
extern int phase23e_logic_eval(int);
extern int phase23e_state_next(int);
extern volatile int phase23e_state_counter;
extern int phase23e_pipeline_eval(int);
static int eq(const char *a, const char *b) {
    unsigned i = 0u;
    if (!a || !b) return 0;
    while (i < 128u && a[i] && a[i] == b[i]) ++i;
    return i < 128u && a[i] == b[i];
}
__attribute__((used,noinline))
int main(int argc, char **argv, char **envp, char **apple) {
    unsigned long sp;
    __asm__ volatile ("mov %0, sp" : "=r"(sp));
    if ((sp & 15ul) || argc != 2 || !argv || !envp || !apple ||
        !eq(argv[0], "phase23e") || !eq(envp[0], "MODE=trusted") ||
        !eq(apple[0], "exec=/App/phase23e") ||
        argv[2] || envp[1] || apple[1]) return 20;
#if defined(PHASE23E_APP_CALC)
    if (!eq(argv[1], "calc") || phase23e_base_value != 7) return 21;
    for (int i = 0; i < 1024; ++i)
        if (phase23e_base_inc(i) != i + 7 ||
            phase23e_math_eval(i) != i + 14 ||
            phase23e_logic_eval(i) != 2 * i + 28) return 22;
#elif defined(PHASE23E_APP_STATE)
    if (!eq(argv[1], "state")) return 23;
    for (int i = 0; i < 1024; ++i)
        if (phase23e_state_next(1) != i + 9) return 24;
    if (phase23e_state_counter != 1025) return 25;
#else
    if (!eq(argv[1], "pipeline")) return 26;
    for (int i = 0; i < 1024; ++i)
        if (phase23e_pipeline_eval(3) != i + 43) return 27;
    if (phase23e_state_counter != 1025) return 28;
#endif
    argv[1][0] = 'x';
    envp[0][0] = 'x';
    apple[0][0] = 'x';
    return 0;
}
#else
#error "a single PHASE23E_* build mode is required"
#endif
