/* Genuine cross-library GOT/data + function binds, not a mock pointer. */
extern int phase19b_base_number;
extern int phase19b_base_calculate(int);
__attribute__((visibility("default"))) int phase19b_leaf_multiplier = 3;
__attribute__((visibility("default")))
int phase19b_leaf_value(void) {
    return phase19b_base_calculate(phase19b_base_number) *
           phase19b_leaf_multiplier;
}
