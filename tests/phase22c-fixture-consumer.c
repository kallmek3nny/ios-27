/* Real external relocation across two compiled ARM64 Mach-O images. */
extern int phase22c_answer(void);
__attribute__((visibility("default"))) int phase22c_call(void) {
    return phase22c_answer() + 1;
}
