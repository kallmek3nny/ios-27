/* A genuine external ARM64 dyld import, not a copied instruction fixture. */
extern int phase17b_answer(void);
__attribute__((visibility("default")))
int phase17b_call(void) {
    return phase17b_answer() + 1;
}
