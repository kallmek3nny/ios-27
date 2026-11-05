typedef unsigned long long u64;
__attribute__((visibility("default")))
u64 phase23d_sum(const unsigned int *values, unsigned long count) {
    if (count > 32) return 0;
    u64 sum = 0;
    for (unsigned long i = 0; i < count; ++i) sum += values[i];
    return sum;
}
