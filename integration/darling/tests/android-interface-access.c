/* Read-only Android interface ioctl availability probe. */
#define _GNU_SOURCE
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return 1;
    struct ifconf list = {0};
    if (ioctl(fd, SIOCGIFCONF, &list) || list.ifc_len < 0 || list.ifc_len > 1024 * 1024) return 2;
    int capacity = list.ifc_len + 2 * (int)sizeof(struct ifreq);
    list.ifc_buf = calloc(1, (size_t)capacity);
    if (!list.ifc_buf) return 3;
    list.ifc_len = capacity;
    if (ioctl(fd, SIOCGIFCONF, &list) || list.ifc_len < 0 || list.ifc_len > capacity ||
            list.ifc_len % sizeof(struct ifreq)) return 4;
    const struct { const char *name; unsigned long operation; } operations[] = {
        {"index", SIOCGIFINDEX}, {"flags", SIOCGIFFLAGS}, {"hardware_address", SIOCGIFHWADDR},
        {"mtu", SIOCGIFMTU}, {"netmask", SIOCGIFNETMASK}, {"broadcast", SIOCGIFBRDADDR}
    };
    for (size_t i = 0; i < (size_t)list.ifc_len / sizeof(struct ifreq); ++i) {
        printf("interface=%.*s", IFNAMSIZ, list.ifc_req[i].ifr_name);
        for (size_t j = 0; j < sizeof(operations) / sizeof(operations[0]); ++j) {
            struct ifreq request = {0};
            memcpy(request.ifr_name, list.ifc_req[i].ifr_name, IFNAMSIZ);
            errno = 0;
            int result = ioctl(fd, operations[j].operation, &request);
            int error = result ? errno : 0;
            printf(" %s=%d/%d", operations[j].name, result, error);
        }
        putchar('\n');
    }
    free(list.ifc_buf);
    close(fd);
    return 0;
}
