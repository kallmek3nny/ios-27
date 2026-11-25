/* Verify that a spawn port action installs the child's bootstrap endpoint. */
#include <crt_externs.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <spawn_private.h>
#include <sys/wait.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
struct message { mach_msg_header_t header; uint64_t value; };

int main(int argc, char **argv) {
    alarm(10);
    mach_port_t port;
    if (argc == 2 && strcmp(argv[1], "--child") == 0) {
        CHECK(task_get_special_port(mach_task_self(), TASK_BOOTSTRAP_PORT, &port) == KERN_SUCCESS);
        CHECK(MACH_PORT_VALID(port));
        struct message message = {0};
        message.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
        message.header.msgh_size = sizeof(message);
        message.header.msgh_remote_port = port;
        message.header.msgh_id = 27031;
        message.value = UINT64_C(0x27123456789);
        CHECK(mach_msg(&message.header, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(message), 0, MACH_PORT_NULL,
                       1000, MACH_PORT_NULL) == KERN_SUCCESS);
        return 0;
    }
    mach_port_t before, after;
    CHECK(task_get_special_port(mach_task_self(), TASK_BOOTSTRAP_PORT, &before) == KERN_SUCCESS);
    CHECK(mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port) == KERN_SUCCESS);
    CHECK(mach_port_insert_right(mach_task_self(), port, port, MACH_MSG_TYPE_MAKE_SEND) == KERN_SUCCESS);
    posix_spawnattr_t attributes;
    CHECK(posix_spawnattr_init(&attributes) == 0);
    CHECK(posix_spawnattr_setspecialport_np(&attributes, port, TASK_BOOTSTRAP_PORT) == 0);
    char executable[4096]; uint32_t capacity = sizeof(executable);
    CHECK(_NSGetExecutablePath(executable, &capacity) == 0);
    char *arguments[] = {executable, "--child", NULL}; pid_t pid;
    CHECK(posix_spawn(&pid, executable, NULL, &attributes, arguments, *_NSGetEnviron()) == 0);
    CHECK(posix_spawnattr_destroy(&attributes) == 0);
    union { struct message message; unsigned char storage[512]; } received = {0};
    kern_return_t result = mach_msg(&received.message.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(received),
                                    port, 2000, MACH_PORT_NULL);
    if (result) fprintf(stderr, "bootstrap receive failed: %#x\n", result);
    int status;
    CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(result == KERN_SUCCESS && received.message.header.msgh_id == 27031 && received.message.value == UINT64_C(0x27123456789));
    CHECK(task_get_special_port(mach_task_self(), TASK_BOOTSTRAP_PORT, &after) == KERN_SUCCESS && before == after);
    CHECK(mach_port_destroy(mach_task_self(), port) == KERN_SUCCESS);
    if (MACH_PORT_VALID(before)) CHECK(mach_port_deallocate(mach_task_self(), before) == KERN_SUCCESS);
    if (MACH_PORT_VALID(after)) CHECK(mach_port_deallocate(mach_task_self(), after) == KERN_SUCCESS);
    printf("DARWIN_SPAWN_PORT_OK checks=%u bootstrap_message=ok parent=unchanged\n", checks);
    return 0;
}
