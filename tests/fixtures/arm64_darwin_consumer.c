/* Independent, SDK-free Clang Darwin object with a real external BL. */
extern int fixture_entry(void);

__attribute__((used, noinline, visibility("default")))
int fixture_consumer(void) {
    return fixture_entry() + 1;
}
