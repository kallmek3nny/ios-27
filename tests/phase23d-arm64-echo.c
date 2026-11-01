/* SDK-free compiler-produced ARM64 Darwin service fixture. Not host-executable. */
typedef unsigned long long u64;
__attribute__((visibility("default")))
u64 phase23d_echo(u64 correlation, const unsigned char *input, unsigned long len,
                  unsigned char *output) {
    if (len > 128 || correlation == 0) return 0;
    for (unsigned long i = 0; i < len; ++i) output[i] = input[i];
    return correlation;
}
