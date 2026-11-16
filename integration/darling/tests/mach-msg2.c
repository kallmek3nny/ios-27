/* Exercise the real packed-argument adapter with a recording Mach transport. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t mach_msg_return_t, mach_msg_option_t, mach_port_t, mach_msg_timeout_t;
typedef struct { uint32_t msgh_bits, msgh_size, msgh_remote_port, msgh_local_port, msgh_voucher_port; int32_t msgh_id; } mach_msg_header_t;
#define MACH_SEND_MSG 1
#define MACH_RCV_MSG 2
#define MACH_PORT_NULL 0
#define MACH_MSG_SUCCESS 0
#define MACH_MSGH_BITS_COMPLEX 0x80000000u
#define MACH_SEND_INVALID_DATA 0x10000002
#define MACH_SEND_MSG_TOO_SMALL 0x10000008
#define MACH_SEND_INVALID_HEADER 0x10000010
#define MACH_RCV_INVALID_DATA 0x10004008
static unsigned checks, calls;
static mach_msg_header_t *sent, *received;
static uint32_t sent_size, received_size, received_port;
static mach_msg_return_t answer;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static mach_msg_return_t mach_msg_overwrite(mach_msg_header_t *s, mach_msg_option_t options, uint32_t ss,
        uint32_t rs, mach_port_t port, mach_msg_timeout_t timeout, mach_port_t notify, mach_msg_header_t *r, uint32_t scatter) {
    CHECK((options & 3) && timeout == 7 && notify == 0 && scatter == 0);
    sent = s; received = r; sent_size = ss; received_size = rs; received_port = port; ++calls;
    return answer;
}
#include MACH_MSG2_HEADER
static uint64_t pair(uint32_t lo, uint32_t hi) { return lo | ((uint64_t)hi << 32); }

int main(void) {
    mach_msg_header_t message = {19, sizeof(message), 42, 0, 0, 27}, receive;
    CHECK(mach_msg2_internal(&message, 3, pair(19, sizeof(message)), 42, pair(0, 27), pair(0, 53), 512, 7) == 0);
    CHECK(sent == &message && received == &message && sent_size == sizeof(message) && received_size == 512 && received_port == 53);
    struct darling_msg_vector *vectors = calloc(2, sizeof(*vectors));
    CHECK(vectors != NULL);
    uint64_t aux[2] = {UINT64_MAX, UINT64_MAX};
    vectors[0] = (struct darling_msg_vector){(uintptr_t)&message, (uintptr_t)&receive, sizeof(message), 256};
    vectors[1] = (struct darling_msg_vector){0, (uintptr_t)aux, 0, sizeof(aux)};
    CHECK(mach_msg2_internal(vectors, 3 | DARLING_MACH64_VECTOR, pair(19, 1), 42, pair(0, 27), pair(0, 53), 2, 7) == 0);
    CHECK(sent == &message && received == &receive && sent_size == sizeof(message) && received_size == 256);
    CHECK(aux[0] == 0 && aux[1] == UINT64_MAX);
    answer = 0x10004003; aux[0] = UINT64_MAX;
    CHECK(mach_msg2_internal(vectors, 2 | DARLING_MACH64_VECTOR, 0, 0, 0, pair(0, 53), 2, 7) == answer);
    CHECK(aux[0] == UINT64_MAX);
    unsigned before = calls;
    for (unsigned count = 0; count <= 4; ++count) {
        if (count == 1 || count == 2) continue;
        CHECK(mach_msg2_internal(vectors, 2 | DARLING_MACH64_VECTOR, 0, 0, 0, 0, count, 7) == MACH_RCV_INVALID_DATA);
        CHECK(mach_msg2_internal(vectors, 1 | DARLING_MACH64_VECTOR, pair(19, count), 0, 0, 0, 0, 7) == MACH_SEND_INVALID_DATA);
    }
    vectors[1].receive_size = 7;
    CHECK(mach_msg2_internal(vectors, 2 | DARLING_MACH64_VECTOR, 0, 0, 0, 0, 2, 7) == MACH_RCV_INVALID_DATA);
    CHECK(mach_msg2_internal(&message, 1, pair(20, sizeof(message)), 42, pair(0, 27), 0, 0, 7) == MACH_SEND_INVALID_HEADER);
    CHECK(mach_msg2_internal(&message, 1, pair(19, 23), 42, pair(0, 27), 0, 0, 7) == MACH_SEND_MSG_TOO_SMALL);
    CHECK(calls == before);
    free(vectors);
    vectors = malloc(sizeof(*vectors)); CHECK(vectors != NULL);
    *vectors = (struct darling_msg_vector){(uintptr_t)&message, 0, 0, 512};
    CHECK(mach_msg2_internal(vectors, 2 | DARLING_MACH64_VECTOR, 0, 0, 0, pair(0, 53), 1, 7) == answer);
    CHECK(received == &message && received_size == 512);
    free(vectors);
    printf("MACH_MSG2_OK checks=%u\n", checks);
    return 0;
}
