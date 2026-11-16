/* Native Darwin sysctl contract for the simulator's boot-argument query. */
#include <sys/types.h>
#include <sys/sysctl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "line %d: %s errno=%d\n", __LINE__, #c, errno); exit(1); } } while (0)

int main(void) {
    size_t size = 0;
    CHECK(sysctlbyname("kern.bootargs", NULL, &size, NULL, 0) == 0 && size == 1);
    char value[3] = {'a', 'b', 'c'};
    size = sizeof(value);
    CHECK(sysctlbyname("kern.bootargs", value, &size, NULL, 0) == 0);
    CHECK(size == 1 && value[0] == '\0' && value[1] == 'b' && value[2] == 'c');

    value[0] = 'a';
    size = 0;
    errno = 0;
    CHECK(sysctlbyname("kern.bootargs", value, &size, NULL, 0) == -1 && errno == ENOMEM);
    CHECK(memcmp(value, "abc", sizeof(value)) == 0);

    const char attempt[] = "-s";
    size = sizeof(value);
    errno = 0;
    CHECK(sysctlbyname("kern.bootargs", value, &size, (void *)attempt, sizeof(attempt)) == -1 && errno == EPERM);
    CHECK(memcmp(value, "abc", sizeof(value)) == 0);
    errno = 0;
    CHECK(sysctlbyname("kern.bootargs", NULL, NULL, (void *)attempt, 0) == -1 && errno == EPERM);

    int oid[CTL_MAXNAME];
    size_t count = CTL_MAXNAME;
    CHECK(sysctlnametomib("kern.bootargs", oid, &count) == 0 && count == 2);
    size = sizeof(value);
    CHECK(sysctl(oid, (unsigned)count, value, &size, NULL, 0) == 0);
    CHECK(size == 1 && value[0] == '\0' && value[1] == 'b' && value[2] == 'c');
    printf("DARWIN_BOOTARGS_OK checks=%u value=empty readonly=yes\n", checks);
    return 0;
}
