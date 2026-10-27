/* Clang emits real external ARM64_RELOC_GOT_LOAD_PAGE21/PAGEOFF12. */
extern const int fixture_data;
__attribute__((used, noinline, visibility("default")))
int fixture_read_data(void) {
    return fixture_data + 2;
}
