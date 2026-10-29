/* SDK-free compiler-generated ARM64 Darwin call sites, NOT guest execution. */
extern int ios27_22a_probe(void *, unsigned long long, unsigned long long,
                           unsigned long long, unsigned);
extern int ios27_22a_map(void *, unsigned long long, unsigned long long,
                         unsigned, unsigned, unsigned long long *);
extern int ios27_22a_protect(void *, unsigned long long, unsigned);
extern int ios27_22a_unmap(void *, unsigned long long);
int phase22a_guest_calls(void *space, unsigned long long guest) {
    unsigned long long mapping = 0;
    int a = ios27_22a_map(space, guest, 4096, 3, 3, &mapping);
    int b = ios27_22a_probe(space, guest, 64, 16, 1);
    int c = ios27_22a_protect(space, mapping, 5);
    int d = ios27_22a_unmap(space, mapping);
    return a | b | c | d;
}
