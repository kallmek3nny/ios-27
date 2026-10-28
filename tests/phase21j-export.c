#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21j_diagnostics.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21j export:%d %s\n", __LINE__, #x); abort(); \
} } while (0)
enum { WRITERS = 4, ITEMS = 10000, TOTAL = WRITERS * ITEMS,
       SNAPSHOTS = 1000 };
typedef struct { ios27_21j_diagnostics *d; unsigned actor; } arg;
static atomic_uint wrote;
static void *writer(void *value) {
    arg *a = value;
    for (unsigned i = 0; i < ITEMS; ++i) {
        CHECK(ios27_21j_record(a->d, IOS27_21J_CHECKPOINT,
                                a->actor, 0, i) == 0);
        atomic_fetch_add_explicit(&wrote, 1u, memory_order_release);
    }
    return NULL;
}
static void validate(const ios27_21j_event *events, size_t n,
                     uint64_t first, uint64_t last) {
    if (n == 0) { CHECK(first == 0 && last == 0); return; }
    CHECK(n <= IOS27_21J_TRACE_CAPACITY);
    CHECK(first != 0 && last - first + 1u == n);
    for (size_t i = 0; i < n; ++i) {
        const ios27_21j_event *entry = &events[i];
        CHECK(entry->sequence == first + i);
        CHECK(entry->kind == IOS27_21J_CHECKPOINT);
        CHECK(entry->actor < WRITERS && entry->result == 0);
        CHECK(entry->detail < ITEMS);
        if (i) CHECK(events[i - 1u].monotonic_ns <= entry->monotonic_ns);
    }
}
static void trace_race(void) {
    ios27_21j_diagnostics *d = NULL;
    pthread_t threads[WRITERS];
    arg args[WRITERS];
    ios27_21j_event *copy;
    ios27_21j_snapshot_data info;
    uint64_t first, last, highest = 0;
    size_t count;
    CHECK(ios27_21j_create(&d, 4, 11, 0) == 0);
    copy = calloc(IOS27_21J_TRACE_CAPACITY, sizeof(*copy));
    CHECK(copy != NULL);
    CHECK(ios27_21j_export(NULL, copy, 4, &count, NULL, NULL) == EINVAL);
    CHECK(ios27_21j_export(d, NULL, 1, &count, NULL, NULL) == EINVAL);
    CHECK(ios27_21j_export(d, copy, 0, NULL, NULL, NULL) == EINVAL);
    CHECK(ios27_21j_export(d, NULL, 0, &count, &first, &last) == 0);
    CHECK(count == 0 && first == 0 && last == 0);
    for (unsigned i = 0; i < WRITERS; ++i) {
        args[i] = (arg){d, i};
        CHECK(pthread_create(&threads[i], NULL, writer, &args[i]) == 0);
    }
    for (unsigned sample = 0; sample < SNAPSHOTS ||
         atomic_load_explicit(&wrote, memory_order_acquire) < TOTAL; ++sample) {
        CHECK(ios27_21j_export(d, copy, IOS27_21J_TRACE_CAPACITY,
                                &count, &first, &last) == 0);
        validate(copy, count, first, last);
        CHECK(last >= highest);
        highest = last;
    }
    for (unsigned i = 0; i < WRITERS; ++i)
        CHECK(pthread_join(threads[i], NULL) == 0);
    CHECK(atomic_load(&wrote) == TOTAL);
    CHECK(ios27_21j_export(d, copy, IOS27_21J_TRACE_CAPACITY,
                            &count, &first, &last) == 0);
    CHECK(count == IOS27_21J_TRACE_CAPACITY);
    CHECK(last == TOTAL && first == TOTAL - count + 1u);
    validate(copy, count, first, last);
    CHECK(ios27_21j_export(d, copy, 17, &count, &first, &last) == ENOSPC);
    CHECK(count == IOS27_21J_TRACE_CAPACITY && last == TOTAL);
    CHECK(ios27_21j_export(d, NULL, 0, &count, NULL, NULL) == ENOSPC);
    CHECK(count == IOS27_21J_TRACE_CAPACITY);
    CHECK(ios27_21j_snapshot(d, &info) == 0);
    CHECK(info.next_sequence == TOTAL);
    CHECK(info.dropped == TOTAL - IOS27_21J_TRACE_CAPACITY);
    CHECK(info.active == 0 && info.leases_created == info.leases_released);
    free(copy);
    CHECK(ios27_21j_destroy(d) == 0);
}
int main(void) {
    trace_race();
    puts("phase21j export: 40k concurrent events and coherent fixed-capacity journal snapshots; PASS");
    return 0;
}
