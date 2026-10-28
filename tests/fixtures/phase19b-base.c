/* Real SDK-free Darwin dylib: exported initialized data and a function. */
__attribute__((visibility("default"))) int phase19b_base_number = 11;
__attribute__((visibility("default")))
int phase19b_base_calculate(int x) { return x + phase19b_base_number; }
