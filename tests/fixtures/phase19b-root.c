/* Four-way real compiler-linked graph: result is 166 on native ARM64. */
extern int phase19b_middle_value(void);
extern int phase19b_leaf_value(void);
extern int phase19b_base_number;
__attribute__((visibility("default"))) int phase19b_root_seed = 5;
__attribute__((visibility("default")))
int phase19b_root_value(void) {
    return phase19b_middle_value() + phase19b_leaf_value() +
           phase19b_base_number + phase19b_root_seed;
}
