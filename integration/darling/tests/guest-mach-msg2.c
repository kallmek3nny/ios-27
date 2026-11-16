/* Exercise the packed Mach ABI against the native server, in both library modes. */
#include <mach/mach.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
struct vector { uint64_t data, receive; uint32_t send_size, receive_size; };
struct message { mach_msg_header_t header; uint64_t value; };
typedef mach_msg_return_t (*msg2_t)(void*, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
static uint64_t pair(uint32_t lo, uint32_t hi) { return lo | ((uint64_t)hi << 32); }

int main(void) {
    alarm(10);
    void *symbol = dlsym(RTLD_DEFAULT, "mach_msg2_internal"); msg2_t msg2;
    CHECK(symbol && sizeof(symbol) == sizeof(msg2)); memcpy(&msg2, &symbol, sizeof(msg2));
    mach_port_t port;
    CHECK(mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port) == KERN_SUCCESS);
    CHECK(mach_port_insert_right(mach_task_self(), port, port, MACH_MSG_TYPE_MAKE_SEND) == KERN_SUCCESS);
    union { struct message message; unsigned char storage[512]; } tx = {0}, rx = {0};
    for (unsigned mode = 0; mode < 2; ++mode) {
        tx.message.header = (mach_msg_header_t){.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0),
            .msgh_size = sizeof(struct message), .msgh_remote_port = port, .msgh_id = 27030};
        tx.message.value = UINT64_C(0x123456789abcdef0) + mode;
        uint64_t aux[2] = {UINT64_MAX, UINT64_MAX};
        struct vector vectors[2] = {{(uintptr_t)&tx, (uintptr_t)&rx, sizeof(struct message), sizeof(rx)},
                                    {0, (uintptr_t)aux, 0, sizeof(aux)}};
        uint64_t options = MACH_SEND_MSG | MACH_RCV_MSG | MACH_SEND_TIMEOUT | MACH_RCV_TIMEOUT;
        if (mode) options |= UINT64_C(0x100000000);
        mach_msg_return_t result = msg2(mode ? (void*)vectors : (void*)&tx, options,
            pair(tx.message.header.msgh_bits, mode ? 1 : sizeof(struct message)), pair(port, 0), pair(0, 27030),
            pair(0, port), mode ? 2 : sizeof(tx), 1000);
        CHECK(result == KERN_SUCCESS);
        struct message *received = mode ? &rx.message : &tx.message;
        CHECK(received->header.msgh_id == 27030 && received->value == UINT64_C(0x123456789abcdef0) + mode);
        if (mode) CHECK(aux[0] == 0 && aux[1] == UINT64_MAX);
    }
    CHECK(msg2(&rx, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, 0, 0, pair(0, port), sizeof(rx), 1) == MACH_RCV_TIMED_OUT);
    CHECK(mach_port_destroy(mach_task_self(), port) == KERN_SUCCESS);
    printf("DARWIN_MACH_MSG2_OK checks=%u scalar=ok vector=ok timeout=ok\n", checks);
    return 0;
}
