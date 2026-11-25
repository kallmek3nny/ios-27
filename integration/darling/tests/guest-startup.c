/* Integrated Darwin startup check, linked against the built Darling libSystem.
 * Uses public pthread and Mach contracts; it runs as a Mach-O guest. */
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int initialized;
static int destroyed;
static int worker_value = 42;
static pthread_key_t key;

__attribute__((constructor)) static void initialize(void) {
    initialized = 27;
}

static void destroy_value(void *value) {
    if (value == &worker_value) destroyed = 1;
}

static void *worker(void *unused) {
    (void)unused;
    if (pthread_setspecific(key, &worker_value) || pthread_getspecific(key) != &worker_value)
        return NULL;
    return &worker_value;
}

int main(int argc, char **argv) {
    const char *marker = getenv("IOS27_GUEST_MARKER");
    if (initialized != 27 || argc != 1 || !argv || !argv[0] || !marker || strcmp(marker, "native-s25")) {
        fputs("FAIL: Darwin constructor/argument/environment startup\n", stderr);
        return 1;
    }
    unsigned char *memory = calloc(4096, 1);
    if (!memory) return 2;
    for (size_t i = 0; i < 4096; ++i) {
        if (memory[i]) { free(memory); return 3; }
    }
    memcpy(memory, "Darwin guest", sizeof("Darwin guest"));
    if (strcmp((char *)memory, "Darwin guest")) { free(memory); return 4; }
    free(memory);

    pthread_t thread;
    int main_value = 7;
    void *returned = NULL;
    if (pthread_key_create(&key, destroy_value) || pthread_setspecific(key, &main_value)
        || pthread_create(&thread, NULL, worker, NULL) || pthread_join(thread, &returned)
        || returned != &worker_value || destroyed != 1 || pthread_getspecific(key) != &main_value) {
        fputs("FAIL: Darwin pthread/TLS lifecycle\n", stderr);
        return 5;
    }
    if (pthread_key_delete(key)) return 6;

    mach_port_t port = MACH_PORT_NULL;
    kern_return_t status = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port);
    if (status == KERN_SUCCESS)
        status = mach_port_insert_right(mach_task_self(), port, port, MACH_MSG_TYPE_MAKE_SEND);
    if (status != KERN_SUCCESS) {
        fprintf(stderr, "FAIL: Darwin Mach port setup: %d\n", status);
        return 7;
    }
    struct message { mach_msg_header_t header; uint64_t value; };
    union { struct message message; unsigned char receive[512]; } buffer = {0};
    buffer.message.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    buffer.message.header.msgh_remote_port = port;
    buffer.message.header.msgh_size = sizeof(struct message);
    buffer.message.header.msgh_id = 27;
    buffer.message.value = UINT64_C(0x2712345678);
    status = mach_msg(&buffer.message.header,
                      MACH_SEND_MSG | MACH_RCV_MSG | MACH_SEND_TIMEOUT | MACH_RCV_TIMEOUT,
                      sizeof(struct message), sizeof(buffer), port, 1000, MACH_PORT_NULL);
    int received = status == KERN_SUCCESS && buffer.message.header.msgh_id == 27
                   && buffer.message.value == UINT64_C(0x2712345678);
    kern_return_t released = mach_port_destroy(mach_task_self(), port);
    if (!received || released != KERN_SUCCESS) {
        fprintf(stderr, "FAIL: Darwin Mach IPC: receive=%d destroy=%d\n", status, released);
        return 8;
    }
    mach_timebase_info_data_t timebase;
    if (mach_timebase_info(&timebase) != KERN_SUCCESS || !timebase.numer || !timebase.denom
        || !mach_absolute_time()) return 9;
    puts("DARWIN_GUEST_STARTUP_OK constructors=ok pthread_tls=ok mach_ipc=ok heap=ok");
    return 0;
}
