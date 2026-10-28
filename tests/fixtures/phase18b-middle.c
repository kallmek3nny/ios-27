/* Two independent imports from a sibling library, including initialized data. */
extern int phase18b_leaf(void);
extern int phase18b_number;
__attribute__((visibility("default"))) int phase18b_adjustment = 2;
__attribute__((visibility("default")))
int phase18b_middle(void) {
    return phase18b_leaf() + phase18b_number + phase18b_adjustment;
}
