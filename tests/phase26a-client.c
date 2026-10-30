#include <stddef.h>
#include <stdint.h>

/* Compiler-produced Mach-O callsites, importing the guest libc from a
 * separate dynamically linked image via LC_LOAD_DYLIB + dyld fixups. */
extern void *malloc(size_t);
extern void *calloc(size_t, size_t);
extern void *realloc(void *, size_t);
extern void free(void *);
extern void *memcpy(void *, const void *, size_t);
extern void *memmove(void *, const void *, size_t);
extern void *memset(void *, int, size_t);
extern int memcmp(const void *, const void *, size_t);
extern size_t strlen(const char *);
extern size_t strnlen(const char *, size_t);
extern int strcmp(const char *, const char *);
extern int strncmp(const char *, const char *, size_t);
extern char *strcpy(char *, const char *);
extern char *strncpy(char *, const char *, size_t);
extern char *strdup(const char *);
extern char *getenv(const char *);
extern int setenv(const char *, const char *, int);
extern int unsetenv(const char *);
#define JOIN0(a,b) a##b
#define JOIN(a,b) JOIN0(a,b)
#define CASE JOIN(phase26a_case_,PHASE26A_CASE)

int CASE(void) {
    char *a = malloc(96u), *b = calloc(2u, 48u);
    int okay = a != NULL && b != NULL;
    if (!okay) return 1;
#if PHASE26A_CASE == 0
    if (memset(a, 0x41, 80u) != a || memcpy(b, a, 80u) != b ||
        memcmp(a, b, 80u) != 0 || memmove(a + 1, a, 48u) != a + 1 ||
        a[48] != 0x41) okay = 0;
#elif PHASE26A_CASE == 1
    a[0] = 'r'; a[1] = 'e'; a[2] = 0;
    char *next = realloc(a, 112u);
    if (!next || next[0] != 'r' || next[1] != 'e' || next[2]) okay = 0;
    else a = next;
    if (calloc(SIZE_MAX, 16u) != NULL) okay = 0;
#elif PHASE26A_CASE == 2
    a[0] = 'X'; a[1] = 0; b[0] = 'Y'; b[1] = 0;
    if (setenv(a, b, 1) || !getenv(a) || strcmp(getenv(a), b) ||
        unsetenv(a) || getenv(a) != NULL) okay = 0;
#elif PHASE26A_CASE == 3
    a[0] = 'a'; a[1] = 'b'; a[2] = 0; a[3] = 'd';
    if (strlen(a) != 2u || strnlen(a, 1u) != 1u ||
        strcpy(b, a) != b || strcmp(a, b) ||
        strncmp(a, b, 2u) || strncpy(b, a, 12u) != b) okay = 0;
    char *duplicate = strdup(a);
    if (!duplicate || strcmp(duplicate, a)) okay = 0;
    free(duplicate);
#elif PHASE26A_CASE == 4
    if (memset(a, 0x7f, 96u) != a ||
        memmove(a + 4, a, 32u) != a + 4 || a[35] != 0x7f ||
        memcpy(a + 2, a, 8u) != NULL) okay = 0;
#else
#error "invalid test case"
#endif
    free(a);
    free(b);
    return okay ? 42 : 2;
}
