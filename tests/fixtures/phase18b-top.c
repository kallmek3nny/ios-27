/* Nontrivial three-image call graph and data bindings via two ordinals. */
extern int phase18b_middle(void);
extern int phase18b_leaf(void);
extern int phase18b_number;
__attribute__((visibility("default")))
int phase18b_top(void) {
    return phase18b_middle() + phase18b_leaf() + phase18b_number + 3;
}
