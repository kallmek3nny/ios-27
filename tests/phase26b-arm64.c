/* SDK-free compiler-authored ARM64 Darwin syscall callsites.
 * This MH_EXECUTE is linked and inspected, NOT executed on Linux. */
typedef unsigned long u64;
__attribute__((noinline)) static u64 raw_read(u64 address, u64 count) {
    register u64 x0 __asm__("x0") = 3u;
    register u64 x1 __asm__("x1") = address;
    register u64 x2 __asm__("x2") = count;
    register u64 x16 __asm__("x16") = 0x02000003u;
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory", "cc");
    return x0;
}
int main(void) {
    return (int)raw_read(0x100000800u, 0u);
}
