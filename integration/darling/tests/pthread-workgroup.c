/* The actual patched pthread registration and forwarding block is included
 * below. Opaque argument types stand in for Darwin's platform declarations. */
#include <errno.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define pthread_t test_pthread_t
#define pthread_attr_t test_pthread_attr_t
#define PTHREAD_WORKGROUP_SPI 1
#include <pthread/workgroup_private.h>

static unsigned checks, calls, crashes;
static jmp_buf crash_return;
static const char *expected_crash;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)

static _Noreturn void client_crash(uintptr_t value, const char *reason) {
    (void)value;
    CHECK(expected_crash && strcmp(reason, expected_crash) == 0);
    ++crashes;
    longjmp(crash_return, 1);
}
#define PTHREAD_CLIENT_CRASH(v, s) client_crash((uintptr_t)(v), (s))
#include PTHREAD_WORKGROUP_BLOCK
#define EXPECT_CRASH(call, reason) do { \
    unsigned before = crashes; \
    expected_crash = (reason); \
    if (setjmp(crash_return) == 0) { call; CHECK(0); } \
    expected_crash = NULL; \
    CHECK(crashes == before + 1); \
} while (0)

_Static_assert(offsetof(struct pthread_workgroup_functions_s, pwgf_version) == 0, "version ABI");
_Static_assert(offsetof(struct pthread_workgroup_functions_s, pwgf_create_with_workgroup) == sizeof(void *), "callback ABI");
_Static_assert(sizeof(struct pthread_workgroup_functions_s) == 2 * sizeof(void *), "v1 prefix ABI");

static int argument, group_token, attr_token, result_token;
static pthread_t thread;
static int callback_result;
static void *start(void *value) { CHECK(value == &argument); return value; }
static int callback(pthread_t *output, os_workgroup_t group,
        const pthread_attr_t *attributes, void *(*routine)(void *), void *arg) {
    ++calls;
    CHECK(output == &thread);
    CHECK((void *)group == &group_token);
    CHECK((const void *)attributes == &attr_token);
    CHECK(routine == start && arg == &argument);
    CHECK(routine(arg) == &argument);
    if (callback_result == 0) *output = (pthread_t)&result_token;
    return callback_result;
}
static int wrong_callback(pthread_t *output, os_workgroup_t group,
        const pthread_attr_t *attributes, void *(*routine)(void *), void *arg) {
    (void)output; (void)group; (void)attributes; (void)routine; (void)arg;
    CHECK(0);
    return -1;
}

int main(int argc, char **argv) {
    CHECK(argc == 1 || (argc == 2 && strcmp(argv[1], "future") == 0));
    os_workgroup_t group = (os_workgroup_t)&group_token;
    const pthread_attr_t *attributes = (const pthread_attr_t *)&attr_token;
    EXPECT_CRASH(pthread_create_with_workgroup_np(&thread, group, attributes, start, &argument),
                 "workgroup functions not yet installed");

    // Old versions have only a version word. ASan checks that rejection does
    // not access a callback field outside the supplied allocation.
    int *old = malloc(sizeof(*old));
    CHECK(old != NULL);
    for (int version = -1; version <= 0; ++version) {
        *old = version;
        EXPECT_CRASH(pthread_install_workgroup_functions_np((const void *)old),
                     "unsupported workgroup functions version");
    }
    free(old);
    EXPECT_CRASH(pthread_create_with_workgroup_np(&thread, group, attributes, start, &argument),
                 "workgroup functions not yet installed");

    // A future table is represented by exactly the known v1 prefix: no later
    // fields are available to read. The real contract retains this table.
    struct pthread_workgroup_functions_s *table = malloc(sizeof(*table));
    CHECK(table != NULL);
    table->pwgf_version = argc == 2 ? 37 : 1;
    table->pwgf_create_with_workgroup = callback;
    pthread_install_workgroup_functions_np(table);
    callback_result = EAGAIN;
    CHECK(pthread_create_with_workgroup_np(&thread, group, attributes, start, &argument) == EAGAIN);
    CHECK(thread == NULL && calls == 1);
    callback_result = 0;
    CHECK(pthread_create_with_workgroup_np(&thread, group, attributes, start, &argument) == 0);
    CHECK((void *)thread == &result_token && calls == 2);

    const struct pthread_workgroup_functions_s duplicate = {1, wrong_callback};
    EXPECT_CRASH(pthread_install_workgroup_functions_np(&duplicate), "workgroup functions already installed");
    CHECK(pthread_create_with_workgroup_np(&thread, group, attributes, start, &argument) == 0);
    CHECK(calls == 3);
    printf("PTHREAD_WORKGROUP_OK version=%d checks=%u\n", table->pwgf_version, checks);
    return 0;
}
