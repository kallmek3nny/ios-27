#include "ios27/phase27b_libsystem.h"
#include <stdint.h>

/* Trusted fixture callback addresses only, no arbitrary guest callbacks.
 * Hard caps prevent unbounded comparator work and stack use. */
#define SORT_MAX_ITEMS 256u
#define SORT_MAX_ELEMENT 256u
#define SORT_MAX_BYTES 4096u
#define EXIT_MAX_CALLBACKS 16u
static void (*callbacks[EXIT_MAX_CALLBACKS])(void);
static size_t callback_count;

static int valid_span(const void *base, size_t count, size_t width) {
    if (!width || width > SORT_MAX_ELEMENT || count > SORT_MAX_ITEMS ||
        count > SORT_MAX_BYTES / width || (count && !base)) {
        *__error() = 22;
        return 0;
    }
    return 1;
}
void qsort(void *base, size_t count, size_t width,
           int (*compare)(const void *, const void *)) {
    unsigned char *items = base;
    unsigned char saved[SORT_MAX_ELEMENT];
    if (!valid_span(base, count, width)) return;
    if (!compare) { *__error() = 22; return; }
    for (size_t i = 1; i < count; ++i) {
        size_t j = i;
        for (size_t k = 0; k < width; ++k) saved[k] = items[i * width + k];
        while (j && compare(items + (j - 1u) * width, saved) > 0) {
            for (size_t k = 0; k < width; ++k)
                items[j * width + k] = items[(j - 1u) * width + k];
            --j;
        }
        for (size_t k = 0; k < width; ++k) items[j * width + k] = saved[k];
    }
}
void *bsearch(const void *key, const void *base, size_t count, size_t width,
              int (*compare)(const void *, const void *)) {
    const unsigned char *items = base;
    size_t low = 0, high = count;
    if (!valid_span(base, count, width)) return NULL;
    if (!key || !compare) { *__error() = 22; return NULL; }
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        int cmp = compare(key, items + middle * width);
        if (cmp == 0) return (void *)(items + middle * width);
        if (cmp < 0) high = middle;
        else low = middle + 1u;
    }
    return NULL;
}
int atexit(void (*handler)(void)) {
    if (!handler || callback_count == EXIT_MAX_CALLBACKS) {
        *__error() = 12;
        return -1;
    }
    callbacks[callback_count++] = handler;
    return 0;
}
void ios27_27b_run_exit_handlers(void) {
    while (callback_count) {
        void (*handler)(void) = callbacks[--callback_count];
        callbacks[callback_count] = NULL;
        handler();
    }
}
