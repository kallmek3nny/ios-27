/* The middle library deliberately has no LC_RPATH. The parent supplies it. */
extern int phase19b_leaf_value(void);
extern int phase19b_base_number;
__attribute__((visibility("default"))) int phase19b_middle_offset = 7;
__attribute__((visibility("default")))
int phase19b_middle_value(void) {
    return phase19b_leaf_value() + phase19b_base_number +
           phase19b_middle_offset;
}
