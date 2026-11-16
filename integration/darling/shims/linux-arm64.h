/* Linux AArch64 syscall ABI for simulator shims executing natively on Android.
 * Numbers and arguments follow asm-generic/unistd.h, futex(2), madvise(2).
 * Results retain Linux's negative errno convention. No libc TLS is accessed. */
#ifndef IOS27_LINUX_ARM64_H
#define IOS27_LINUX_ARM64_H

#if !defined(__aarch64__) && !defined(__arm64__)
#error "Native simulator shims require ARM64"
#endif

static inline long ios27_linux_syscall3(long number, long a, long b, long c) {
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x8 __asm__("x8") = number;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory", "cc");
    return x0;
}

static inline long ios27_linux_futex_wake_shared(void *address, int count) {
    return ios27_linux_syscall3(98, (long)address, 1 /* FUTEX_WAKE */, count);
}

static inline long ios27_linux_madvise_remove(void *address, unsigned long length) {
    return ios27_linux_syscall3(233, (long)address, (long)length, 9 /* MADV_REMOVE */);
}

#endif
