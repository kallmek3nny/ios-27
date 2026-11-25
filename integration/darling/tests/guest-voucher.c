/* A real voucher object must retain and return the supplied user-data bytes. */
#include <mach/mach.h>
#include <mach/mach_voucher.h>
#include <mach/mach_voucher_types.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)

int main(void) {
    struct recipe { uint32_t key, command, previous, content_size; uint64_t value; } data = {
        .key = MACH_VOUCHER_ATTR_KEY_USER_DATA, .command = MACH_VOUCHER_ATTR_USER_DATA_STORE,
        .previous = MACH_PORT_NULL, .content_size = sizeof(uint64_t),
        .value = UINT64_C(0x2711223344556677)};
    _Static_assert(offsetof(struct recipe, value) == sizeof(mach_voucher_attr_recipe_data_t), "recipe ABI");
    mach_port_t voucher = MACH_PORT_NULL;
    kern_return_t status = host_create_mach_voucher(mach_host_self(), (void *)&data, sizeof(data), &voucher);
    if (status) fprintf(stderr, "voucher creation status=%d\n", status);
    CHECK(status == KERN_SUCCESS && MACH_PORT_VALID(voucher));
    unsigned char returned[64] = {0};
    mach_voucher_attr_content_size_t length = sizeof(returned);
    CHECK(mach_voucher_extract_attr_content(voucher, MACH_VOUCHER_ATTR_KEY_USER_DATA, returned, &length) == KERN_SUCCESS);
    CHECK(length == sizeof(data.value) && memcmp(returned, &data.value, length) == 0);
    CHECK(mach_port_deallocate(mach_task_self(), voucher) == KERN_SUCCESS);
    voucher = MACH_PORT_NULL;
    CHECK(host_create_mach_voucher(mach_host_self(), NULL, 0, &voucher) == KERN_SUCCESS && voucher == MACH_PORT_NULL);
    printf("DARWIN_VOUCHER_OK checks=%u user_data=roundtrip empty=ok\n", checks);
    return 0;
}
