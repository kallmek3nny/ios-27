#include <stdint.h>
volatile uint64_t ios27_24d_echo_calls=0;
uint64_t ios27_24d_echo(uint64_t x) { ios27_24d_echo_calls++;return x ^ UINT64_C(0x243d24d); }
