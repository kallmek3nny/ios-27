#include <stdint.h>
extern uint64_t ios27_24d_sum(uint64_t,uint64_t);
extern int64_t ios27_24d_native_service_bridge(uint64_t,uint64_t);
int main(void) {uint64_t x=ios27_24d_sum(9,4);return (int)(x^ios27_24d_native_service_bridge(4,x));}
