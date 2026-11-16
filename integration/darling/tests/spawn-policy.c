/* Real bounded attribute decoder and private environment parser. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "darling_spawn_policy.h"
static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)

int main(void) {
    int value = 8;
    CHECK(darling_spawn_read_policy(NULL, SIZE_MAX, &value) == 0 && value == -1);
    for (size_t length = 0; length < 136; ++length) {
        unsigned char *buffer = malloc(length ? length : 1);
        CHECK(buffer != NULL);
        memset(buffer, 0xff, length);
        CHECK(darling_spawn_read_policy(buffer, length, &value) == (length < 132 ? 0 : -EINVAL));
        CHECK(value == -1);
        free(buffer);
    }
    unsigned char *buffer = calloc(136, 1);
    CHECK(buffer != NULL);
    uint32_t options = 1;
    memcpy(buffer + 128, &options, 4);
    CHECK(darling_spawn_read_policy(buffer, 132, &value) == 0 && value == -1);
    options |= 4;
    memcpy(buffer + 128, &options, 4);
    for (int policy = -1; policy <= 3; ++policy) {
        memcpy(buffer + 132, &policy, 4);
        CHECK(darling_spawn_read_policy(buffer, 136, &value) == ((policy < 0 || policy > 2) ? -EINVAL : 0));
        CHECK(value == ((policy < 0 || policy > 2) ? -1 : policy));
    }
    free(buffer);
    CHECK(darling_dataless_env_policy(NULL) == -1);
    const char *valid[] = {DARLING_DATALESS_ENV "=0", DARLING_DATALESS_ENV "=1", DARLING_DATALESS_ENV "=2"};
    for (int i = 0; i < 3; ++i) {
        const char *environment[] = {"OTHER=1", valid[i], NULL};
        CHECK(darling_dataless_env_policy(environment) == i);
    }
    const char *invalid[] = {"", "_", DARLING_DATALESS_ENV, DARLING_DATALESS_ENV "=", DARLING_DATALESS_ENV "=-1",
                            DARLING_DATALESS_ENV "=3", DARLING_DATALESS_ENV "=1x", DARLING_DATALESS_ENV "x=1"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
        char *text = malloc(strlen(invalid[i]) + 1);
        CHECK(text != NULL);
        strcpy(text, invalid[i]);
        const char *environment[] = {text, NULL};
        CHECK(darling_dataless_env_policy(environment) == -1);
        free(text);
    }
    printf("SPAWN_POLICY_OK checks=%u\n", checks);
    return 0;
}
