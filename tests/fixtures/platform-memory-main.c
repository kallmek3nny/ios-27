#define IOS27_PLATFORM_EXPORT_DARWIN 1
#include "ios27/platform_memory.h"

/* Original SDK-free client. All twelve imports resolve through an actual
 * separate Mach-O dylib. No host callback table or Apple implementation. */
int main(void) {
    char a[48], b[48];
    if (memset(a, 0xa5, sizeof(a)) != a) return 101;
    if (strcpy(a + 1, "platform bridge") != a + 1) return 102;
    if (strlen(a + 1) != 15u || strnlen(a + 1, 8u) != 8u) return 103;
    if (strcmp(a + 1, "platform bridge") || strncmp(a + 1, "platform", 8u)) return 104;
    if (strstr(a + 1, "bridge") != a + 10 || strstr(a + 1, "missing")) return 105;
    if (memcpy(b, a, sizeof(a)) != b || memcmp(a, b, sizeof(a))) return 106;
    if (memmove(b + 1, b, sizeof(b) - 1u) != b + 1 || b[2] != 'p') return 107;
    if (strncpy(b, "ok", 8u) != b || b[2] || b[7]) return 108;
    bzero(b, sizeof(b));
    for (size_t i = 0u; i < sizeof(b); ++i) if (b[i]) return 109;
    if ((unsigned char)a[0] != 0xa5u || (unsigned char)a[47] != 0xa5u) return 110;
    return 42;
}
