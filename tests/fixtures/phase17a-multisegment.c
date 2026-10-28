/* Original source compiled by Clang for arm64-apple-darwin, without SDK. */
__attribute__((used, section("__DATA_CONST,__const")))
static const volatile int constant = 37;
__attribute__((used)) static volatile int counter = 5;
__attribute__((used)) static volatile int zero_tail[8];

__attribute__((used, noinline))
int main(void) {
    /* Requires linked ADRP-based PC-relative addressing across multiple
     * segments, read-only constants, initialized RW data and zero-filled BSS. */
    int n = constant + counter + zero_tail[7];
    counter = n;
    zero_tail[7] = counter;
    return counter;
}
