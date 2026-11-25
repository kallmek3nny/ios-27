/* Original SDK-free Darwin fixture. Both constructors must run exactly once,
 * in table order, before the importing executable's constructor. */
volatile int initializer_provider_value;

/* Original declaration of the documented five-pointer Darwin ABI. Keep
 * this independent of the host header so the fixture verifies the layout. */
struct ProgramVars {
    const unsigned *mh;
    int *argc;
    const char ***argv;
    const char ***environ;
    const char **progname;
};
static const struct ProgramVars *seen_vars;
static const char *seen_name;

__attribute__((constructor(101)))
static void provider_first(int argc, const char *const *argv,
                           const char *const *envp, const char *const *apple,
                           const struct ProgramVars *vars) {
    if (argc != 2 || !argv || !argv[0] || argv[0][0] != 'f' ||
        !envp || !envp[0] || envp[0][0] != 'E' ||
        !apple || !apple[0] || apple[0][0] != 'A' || initializer_provider_value) {
        initializer_provider_value = -100;
        return;
    }
    if (!vars || !vars->mh || vars->mh[0] != 0xfeedfacfu || vars->mh[3] != 2u ||
        !vars->argc || *vars->argc != argc || !vars->argv || *vars->argv != argv ||
        !vars->environ || *vars->environ != envp || !vars->progname ||
        !*vars->progname || (*vars->progname)[0] != 'f') {
        initializer_provider_value = -110;
        return;
    }
    seen_vars = vars;
    seen_name = *vars->progname;
    *vars->argc = argc + 1;
    *vars->progname = seen_name + 1;
    if (argv[1][0] == 't') __builtin_debugtrap();
    if (argv[1][0] == 'l') for (;;) __asm__ volatile("");
    if (argv[1][0] == 's') __asm__ volatile("mov x8, #172\nsvc #0" ::: "x0", "x8", "memory");
    initializer_provider_value = 10;
}

__attribute__((constructor(102)))
static void provider_second(void) {
    if (initializer_provider_value != 10 || !seen_vars || *seen_vars->argc != 3 ||
        *seen_vars->progname != seen_name + 1) {
        initializer_provider_value = -101;
        return;
    }
    *seen_vars->argc = 2;
    *seen_vars->progname = seen_name;
    initializer_provider_value = 40;
}
