/* Independent linked dependency on the echo provider. */
typedef unsigned long long u64;
extern u64 phase23d_echo(u64, const unsigned char *, unsigned long, unsigned char *);
__attribute__((visibility("default")))
u64 phase23d_relay(u64 correlation, const unsigned char *input, unsigned long len,
                   unsigned char *output) {
    if (!correlation) return 0;
    return phase23d_echo(correlation, input, len, output);
}
