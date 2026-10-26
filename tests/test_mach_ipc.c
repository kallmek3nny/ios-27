#include "ios27/mach_ipc.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(ios27_mach_header) == 24u, "Mach wire header");
_Static_assert(sizeof(ios27_mach_port_descriptor) == 12u, "Mach wire descriptor");

static void le16(uint8_t *p, uint16_t n) {
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8u);
}
static void le32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0u; i < 4u; ++i)
        p[i] = (uint8_t)(n >> (i * 8u));
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static size_t make_simple(uint8_t *p, uint32_t destination,
                          int32_t id, size_t payload_len) {
    size_t len = 24u + payload_len;
    assert(payload_len % 4u == 0u && len <= IOS27_MACH_MSG_MAX);
    memset(p, 0, len);
    le32(p, IOS27_MACH_COPY_SEND);
    le32(p + 4u, (uint32_t)len);
    le32(p + 8u, destination);
    le32(p + 20u, (uint32_t)id);
    for (size_t i = 0u; i < payload_len; ++i)
        p[24u + i] = (uint8_t)(i + 1u);
    return len;
}
static size_t make_complex(uint8_t *p, uint32_t destination,
                           uint32_t transferred, uint8_t disposition) {
    size_t size = make_simple(p, destination, 2027, 16u);
    (void)size;
    le32(p, IOS27_MACH_COMPLEX | IOS27_MACH_COPY_SEND);
    le32(p + 24u, 1u); /* descriptor count */
    le32(p + 28u, transferred);
    le32(p + 32u, 0u); /* pad1 */
    le16(p + 36u, 0u); /* pad2 */
    p[38u] = disposition;
    p[39u] = IOS27_MACH_PORT_DESCRIPTOR;
    return 40u;
}
static uint32_t refs(const ios27_mach_space *space,
                     uint32_t task, uint32_t port) {
    uint32_t count = 0u;
    assert(ios27_mach_send_refs(space, task, port, &count) == IOS27_MACH_OK);
    return count;
}

static void basics(void) {
    ios27_mach_space *space = ios27_mach_space_new();
    uint8_t message[IOS27_MACH_MSG_MAX], output[IOS27_MACH_MSG_MAX];
    uint32_t port = 0u;
    size_t size, cap;
    assert(space != NULL);
    assert(ios27_mach_port_allocate(space, 1u, &port) == IOS27_MACH_OK);
    assert(port != 0u);
    size = make_simple(message, port, -304, 8u);
    assert(ios27_mach_send(space, 2u, message, size, 0u) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_port_make_send(space, 2u, port, 2u) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_port_make_send(space, 1u, port, 2u) ==
           IOS27_MACH_OK);
    assert(refs(space, 2u, port) == 1u);
    assert(ios27_mach_receive(space, 2u, port, output, &size, 0u) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_send(space, 2u, message, size, 0u) ==
           IOS27_MACH_OK);
    assert(refs(space, 2u, port) == 1u);
    cap = size - 1u;
    memset(output, 0xbdu, sizeof(output));
    assert(ios27_mach_receive(space, 1u, port, output, &cap, 0u) ==
           IOS27_MACH_TOO_LARGE);
    assert(cap == size && output[0] == 0xbdu);
    cap = sizeof(output);
    assert(ios27_mach_receive(space, 1u, port, output, &cap, 0u) ==
           IOS27_MACH_OK);
    assert(cap == size);
    assert(rd32(output) == 0u && rd32(output + 4u) == size);
    assert(rd32(output + 8u) == 0u && rd32(output + 12u) == port);
    assert(rd32(output + 20u) == (uint32_t)-304);
    assert(memcmp(output + 24u, message + 24u, 8u) == 0);
    assert(ios27_mach_receive(space, 1u, port, output, &cap, 17u) ==
           IOS27_MACH_TIMEOUT);
    assert(ios27_mach_port_drop_send(space, 1u, port) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_port_drop_send(space, 2u, port) ==
           IOS27_MACH_OK);
    assert(refs(space, 2u, port) == 0u);
    assert(ios27_mach_send(space, 2u, message, size, 0u) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_port_destroy(space, 2u, port) ==
           IOS27_MACH_BAD_RIGHT);
    assert(ios27_mach_port_destroy(space, 1u, port) == IOS27_MACH_OK);
    assert(ios27_mach_port_make_send(space, 1u, port, 2u) ==
           IOS27_MACH_BAD_RIGHT);
    ios27_mach_space_free(space);
}

static void queue_and_validation(void) {
    ios27_mach_space *space = ios27_mach_space_new();
    uint8_t msg[IOS27_MACH_MSG_MAX], out[IOS27_MACH_MSG_MAX];
    uint32_t port = 0u;
    size_t len, cap;
    assert(space);
    assert(ios27_mach_port_allocate(space, 1u, &port) == IOS27_MACH_OK);
    assert(ios27_mach_port_make_send(space, 1u, port, 2u) ==
           IOS27_MACH_OK);
    len = make_simple(msg, port, 1, 0u);
    for (size_t i = 0u; i < IOS27_MACH_QUEUE_MAX; ++i) {
        le32(msg + 20u, (uint32_t)(i + 11u));
        assert(ios27_mach_send(space, 2u, msg, len, 0u) ==
               IOS27_MACH_OK);
    }
    assert(ios27_mach_send(space, 2u, msg, len, 5u) ==
           IOS27_MACH_TIMEOUT);
    for (size_t i = 0u; i < IOS27_MACH_QUEUE_MAX; ++i) {
        cap = sizeof(out);
        assert(ios27_mach_receive(space, 1u, port, out, &cap, 0u) ==
               IOS27_MACH_OK);
        assert(rd32(out + 20u) == i + 11u);
    }
    assert(ios27_mach_receive(space, 1u, port, out, &cap, 10u) ==
           IOS27_MACH_TIMEOUT);
    len = make_simple(msg, port, 1, 0u);
    le32(msg + 4u, (uint32_t)len + 4u);
    assert(ios27_mach_send(space, 2u, msg, len, 0u) ==
           IOS27_MACH_UNSUPPORTED);
    le32(msg + 4u, (uint32_t)len);
    le32(msg + 12u, port); /* reply ports unsupported */
    assert(ios27_mach_send(space, 2u, msg, len, 0u) ==
           IOS27_MACH_UNSUPPORTED);
    le32(msg + 12u, 0u);
    le32(msg + 16u, port); /* vouchers unsupported */
    assert(ios27_mach_send(space, 2u, msg, len, 0u) ==
           IOS27_MACH_UNSUPPORTED);
    le32(msg + 16u, 0u);
    le32(msg, 999u);
    assert(ios27_mach_send(space, 2u, msg, len, 0u) ==
           IOS27_MACH_UNSUPPORTED);
    le32(msg, IOS27_MACH_COPY_SEND);
    assert(ios27_mach_send(space, 2u, msg, len - 1u, 0u) ==
           IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_send(space, 0u, msg, len, 0u) ==
           IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_send(NULL, 2u, msg, len, 0u) ==
           IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_send(space, 2u, NULL, len, 0u) ==
           IOS27_MACH_BAD_ARGUMENT);
    assert(ios27_mach_receive(space, 1u, port, out, &cap, 0u) ==
           IOS27_MACH_TIMEOUT);
    assert(ios27_mach_port_destroy(space, 1u, port) == IOS27_MACH_OK);
    ios27_mach_space_free(space);
}
static void transfer_test(void) {
    ios27_mach_space *space = ios27_mach_space_new();
    uint32_t inbox = 0u, item = 0u;
    uint8_t msg[IOS27_MACH_MSG_MAX], out[IOS27_MACH_MSG_MAX];
    size_t len, cap;
    assert(space);
    assert(ios27_mach_port_allocate(space, 1u, &inbox) == IOS27_MACH_OK);
    assert(ios27_mach_port_allocate(space, 3u, &item) == IOS27_MACH_OK);
    assert(ios27_mach_port_make_send(space, 1u, inbox, 2u) == IOS27_MACH_OK);
    assert(ios27_mach_port_make_send(space, 3u, item, 2u) == IOS27_MACH_OK);
    len = make_complex(msg, inbox, item, IOS27_MACH_MOVE_SEND);
    assert(refs(space, 2u, item) == 1u);
    assert(ios27_mach_send(space, 2u, msg, len, 0u) == IOS27_MACH_OK);
    assert(refs(space, 2u, item) == 0u);
    assert(refs(space, 1u, item) == 0u);
    assert(ios27_mach_port_destroy(space, 3u, item) == IOS27_MACH_BAD_RIGHT);
    cap = 16u; /* too-small receive keeps right in flight */
    assert(ios27_mach_receive(space, 1u, inbox, out, &cap, 0u) ==
           IOS27_MACH_TOO_LARGE);
    assert(cap == len && refs(space, 1u, item) == 0u);
    cap = sizeof(out);
    assert(ios27_mach_receive(space, 1u, inbox, out, &cap, 0u) ==
           IOS27_MACH_OK);
    assert(cap == len && rd32(out) == IOS27_MACH_COMPLEX);
    assert(rd32(out + 12u) == inbox);
    assert(rd32(out + 28u) == item && out[38u] == IOS27_MACH_MOVE_SEND);
    assert(refs(space, 1u, item) == 1u && refs(space, 2u, item) == 0u);

    assert(ios27_mach_port_make_send(space, 3u, item, 2u) == IOS27_MACH_OK);
    len = make_complex(msg, inbox, item, IOS27_MACH_COPY_SEND);
    assert(ios27_mach_send(space, 2u, msg, len, 0u) == IOS27_MACH_OK);
    assert(refs(space, 2u, item) == 1u && refs(space, 1u, item) == 1u);
    cap = sizeof(out);
    assert(ios27_mach_receive(space, 1u, inbox, out, &cap, 0u) ==
           IOS27_MACH_OK);
    assert(refs(space, 2u, item) == 1u && refs(space, 1u, item) == 2u);

    len = make_complex(msg, inbox, item, IOS27_MACH_MOVE_SEND);
    msg[39u] = 1u; /* unsupported descriptor type */
    assert(ios27_mach_send(space, 2u, msg, len, 0u) ==
           IOS27_MACH_UNSUPPORTED);
    assert(refs(space, 2u, item) == 1u);
    msg[39u] = IOS27_MACH_PORT_DESCRIPTOR;
    le32(msg + 24u, 2u); /* more than one descriptor */
    assert(ios27_mach_send(space, 2u, msg, len, 0u) ==
           IOS27_MACH_UNSUPPORTED);
    le32(msg + 24u, 1u);
    le32(msg + 32u, 99u); /* nonzero padding */
    assert(ios27_mach_send(space, 2u, msg, len, 0u) ==
           IOS27_MACH_UNSUPPORTED);
    le32(msg + 32u, 0u);
    msg[38u] = 20u; /* MAKE_SEND unsupported */
    assert(ios27_mach_send(space, 2u, msg, len, 0u) ==
           IOS27_MACH_UNSUPPORTED);
    msg[38u] = IOS27_MACH_MOVE_SEND;
    le32(msg + 28u, 99999u);
    assert(ios27_mach_send(space, 2u, msg, len, 0u) ==
           IOS27_MACH_BAD_RIGHT);
    le32(msg + 28u, item);
    assert(refs(space, 2u, item) == 1u);

    /* Destroying an inbox discards its queued in-flight transfer safely. */
    assert(ios27_mach_send(space, 2u, msg, len, 0u) == IOS27_MACH_OK);
    assert(refs(space, 2u, item) == 0u);
    assert(ios27_mach_port_destroy(space, 1u, inbox) == IOS27_MACH_OK);
    assert(ios27_mach_port_destroy(space, 3u, item) == IOS27_MACH_OK);
    assert(ios27_mach_send_refs(space, 1u, item, &inbox) ==
           IOS27_MACH_BAD_RIGHT);
    ios27_mach_space_free(space);
}
static void quotas_and_stale_names(void) {
    ios27_mach_space *space = ios27_mach_space_new();
    uint32_t names[IOS27_MACH_PORT_MAX], fresh = 0u, original = 0u;
    assert(space);
    for (size_t i = 0u; i < IOS27_MACH_PORT_MAX; ++i)
        assert(ios27_mach_port_allocate(space, 1u, &names[i]) == IOS27_MACH_OK);
    assert(ios27_mach_port_allocate(space, 1u, &fresh) == IOS27_MACH_LIMIT);
    original = names[3u];
    assert(ios27_mach_port_destroy(space, 1u, original) == IOS27_MACH_OK);
    assert(ios27_mach_port_allocate(space, 1u, &fresh) == IOS27_MACH_OK);
    assert(fresh != original);
    assert(ios27_mach_port_make_send(space, 1u, original, 2u) ==
           IOS27_MACH_BAD_RIGHT);
    for (unsigned i = 0u; i < IOS27_MACH_RIGHT_MAX; ++i)
        assert(ios27_mach_port_make_send(space, 1u, fresh, 2u) ==
               IOS27_MACH_OK);
    assert(ios27_mach_port_make_send(space, 1u, fresh, 2u) ==
           IOS27_MACH_LIMIT);
    assert(refs(space, 2u, fresh) == IOS27_MACH_RIGHT_MAX);
    assert(ios27_mach_port_drop_send(space, 2u, fresh) == IOS27_MACH_OK);
    assert(refs(space, 2u, fresh) == IOS27_MACH_RIGHT_MAX - 1u);
    ios27_mach_space_free(space);
}

int main(void) {
    basics();
    queue_and_validation();
    transfer_test();
    quotas_and_stale_names();
    puts("synthetic Mach port rights and bounded message tests passed");
    return 0;
}
