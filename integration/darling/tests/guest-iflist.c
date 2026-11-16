/* Bounded route-interface query followed by launchd_sim's resolver warmup. */
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <net/route.h>
#include <netdb.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    alarm(10);
    int mib[] = {CTL_NET, PF_ROUTE, 0, AF_UNSPEC, NET_RT_IFLIST, 0};
    size_t required = 0;
    int result = sysctl(mib, 6, NULL, &required, NULL, 0);
    printf("IFLIST size result=%d errno=%d bytes=%zu\n", result, errno, required);
    fflush(stdout);
    if (result || !required || required > 1024 * 1024) return 1;
    unsigned char *data = NULL;
    size_t used = 0;
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        size_t capacity = required * (1u << attempt);
        if (capacity > 4 * 1024 * 1024) return 2;
        data = malloc(capacity);
        if (!data) return 3;
        memset(data, 0xa5, capacity);
        used = capacity;
        errno = 0;
        result = sysctl(mib, 6, data, &used, NULL, 0);
        int error = errno;
        printf("IFLIST fill attempt=%u result=%d errno=%d capacity=%zu bytes=%zu\n",
               attempt, result, error, capacity, used);
        fflush(stdout);
        if (!result) break;
        free(data); data = NULL;
        if (error != ENOMEM) return 4;
        if (used > required) required = used;
    }
    if (result || !data) return 5;
    unsigned messages = 0;
    for (size_t offset = 0; offset < used; ++messages) {
        if (used - offset < 4) return 6;
        uint16_t length;
        memcpy(&length, data + offset, sizeof(length));
        if (length < 4 || length > used - offset || data[offset + 2] != RTM_VERSION) return 7;
        offset += length;
    }
    free(data);
    struct addrinfo hints = {0}, *addresses = NULL;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    result = getaddrinfo(NULL, "1138", &hints, &addresses);
    printf("IFLIST resolver result=%d\n", result);
    if (result || !addresses) return 8;
    freeaddrinfo(addresses);
    printf("DARWIN_IFLIST_OK bytes=%zu messages=%u\n", used, messages);
    return 0;
}
