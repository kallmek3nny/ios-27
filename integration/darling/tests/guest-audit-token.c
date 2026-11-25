/* Task audit identity must agree with public IDs and Mach message trailers. */
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)

int main(void) {
    audit_token_t token;
    mach_msg_type_number_t count = TASK_AUDIT_TOKEN_COUNT;
    CHECK(task_info(mach_task_self(), TASK_AUDIT_TOKEN, (task_info_t)&token, &count) == KERN_SUCCESS);
    CHECK(count == TASK_AUDIT_TOKEN_COUNT && token.val[5] == (unsigned)getpid() && token.val[7] != 0);
    CHECK(token.val[1] == geteuid() && token.val[3] == getuid());
    CHECK(token.val[2] == getegid() && token.val[4] == getgid());
    mach_port_t port;
    CHECK(mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port) == KERN_SUCCESS);
    CHECK(mach_port_insert_right(mach_task_self(), port, port, MACH_MSG_TYPE_MAKE_SEND) == KERN_SUCCESS);
    union { mach_msg_header_t header; unsigned char data[512]; } message = {0};
    message.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    message.header.msgh_size = sizeof(mach_msg_header_t);
    message.header.msgh_remote_port = port;
    message.header.msgh_id = 27032;
    CHECK(mach_msg(&message.header, MACH_SEND_MSG | MACH_RCV_MSG | MACH_RCV_TIMEOUT |
                   MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0) | MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT),
                   sizeof(mach_msg_header_t), sizeof(message), port, 1000, MACH_PORT_NULL) == KERN_SUCCESS);
    mach_msg_audit_trailer_t *trailer = (void *)(message.data + ((message.header.msgh_size + 3) & ~3u));
    CHECK(trailer->msgh_trailer_size >= sizeof(*trailer));
    CHECK(memcmp(&token, &trailer->msgh_audit, sizeof(token)) == 0);
    CHECK(mach_port_destroy(mach_task_self(), port) == KERN_SUCCESS);
    audit_token_t again; count = TASK_AUDIT_TOKEN_COUNT;
    CHECK(task_info(mach_task_self(), TASK_AUDIT_TOKEN, (task_info_t)&again, &count) == KERN_SUCCESS);
    CHECK(memcmp(&token, &again, sizeof(token)) == 0);
    printf("DARWIN_AUDIT_TOKEN_OK checks=%u pid=%u generation=%u uid=%u gid=%u trailer=ok\n",
           checks, token.val[5], token.val[7], token.val[1], token.val[2]);
    return 0;
}
