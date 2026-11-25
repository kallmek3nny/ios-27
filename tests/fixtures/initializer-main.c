/* Original SDK-free importing executable: native entry returns 42 only after
 * two provider constructors and this importing constructor have run. */
extern volatile int initializer_provider_value;
volatile int initializer_main_value;

__attribute__((constructor))
static void importing_constructor(void) {
    initializer_main_value = initializer_provider_value == 40 ? 42 : -102;
    initializer_provider_value += 1;
}

int main(int argc, char **argv, char **envp, char **apple) {
    unsigned long stack;
    __asm__ volatile("mov %0, sp" : "=r"(stack));
    if (stack % 16 || argc != 2 || !argv || argv[2] || !envp || envp[1] || !apple || apple[1])
        return -103;
    if (initializer_provider_value != 41) return -104;
    return initializer_main_value;
}
