/* Freestanding, SDK-free source linked into a genuine ARM64 MH_DYLIB. */
__attribute__((visibility("default"))) int phase18b_number = 40;
__attribute__((visibility("default")))
int phase18b_leaf(void) { return phase18b_number + 1; }
