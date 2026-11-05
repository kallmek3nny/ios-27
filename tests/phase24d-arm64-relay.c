#include <stdint.h>
extern uint64_t ios27_24d_sum(uint64_t,uint64_t);
uint64_t ios27_24d_relay(uint64_t x) {return ios27_24d_sum(x,7)+ios27_24d_sum(3,x);}
