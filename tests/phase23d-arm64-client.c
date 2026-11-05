/* Real ARM64 Darwin client linked against independently built dylibs.
 * A synthetic entrypoint: not launched by Linux or a guest interpreter. */
typedef unsigned long long u64;
extern u64 phase23d_echo(u64, const unsigned char *, unsigned long, unsigned char *);
extern u64 phase23d_sum(const unsigned int *, unsigned long);
extern u64 phase23d_relay(u64, const unsigned char *, unsigned long, unsigned char *);
static volatile u64 phase23d_client_result;
__attribute__((visibility("default")))
void phase23d_start(void) {
    const unsigned char input[4] = {2, 7, 3, 5};
    unsigned char output[4] = {0};
    const unsigned int sums[3] = {11, 22, 33};
    u64 a = phase23d_echo(1, input, 4, output);
    u64 b = phase23d_sum(sums, 3);
    u64 c = phase23d_relay(2, input, 4, output);
    phase23d_client_result = a + b + c + (u64)output[0];
    for (;;) __asm__ volatile("wfe");
}
