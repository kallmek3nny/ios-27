#include <stdint.h>
extern uint64_t ios27_24d_relay(uint64_t);
uint64_t ios27_24d_worker(uint64_t x) {return ios27_24d_relay(x)+ios27_24d_relay(x+1);}
