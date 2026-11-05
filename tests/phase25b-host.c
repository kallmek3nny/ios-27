#include "ios27/phase25b_dispatch.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static phase25b_runtime *runtime;
static void *root_token, *child_token, *leaf_token;
static phase25b_sel add_selector, class_selector;
static uint32_t image0, image1;
static void *obj;

static void *worker(void *unused) {
    (void)unused;
    for (size_t i = 0; i < 30000; ++i) {
        phase25b_ticket ticket;
        void *imp = phase25b_prepare(obj, add_selector, &ticket, 0);
        assert(imp && ticket.receiver == obj && ticket.runtime == runtime);
        assert(ticket.image_slot_plus_one != 0);
        phase25b_finish(&ticket);
    }
    return NULL;
}
static void encodings(void) {
    assert(phase25b_parse_encoding("Q16@0:8") == PHASE25B_WORD0);
    assert(phase25b_parse_encoding("Q24@0:8Q16") == PHASE25B_WORD1);
    assert(phase25b_parse_encoding("^v24@0:8^v16") == PHASE25B_WORD1);
    assert(phase25b_parse_encoding("Q64@0:8Q16Q24Q32Q40Q48Q56") == PHASE25B_WORD6);
    assert(phase25b_parse_encoding("f24@0:8f16") == PHASE25B_FLOAT1);
    assert(phase25b_parse_encoding("d24@0:8d16") == PHASE25B_DOUBLE1);
    assert(phase25b_parse_encoding("v16@0:8") == PHASE25B_VOID0);
    const char *rejects[] = {
        "", "Q24@0:8i16", "Q24@0:8@?16", "{Pair=QQ}16@0:8",
        "Q24@0:8d16", "Q24@0:8Q12", "Q16@0:0", "Q8@0:8",
        "Q72@0:8Q16Q24Q32Q40Q48Q56Q64", "d24@0:8f16",
        "d32@0:8d16Q24", "v24@0:8Q16", "Q24@0:8^@16",
        "Q24@0:8[2Q]16", "Q24@0:8Q16...", "Q24@0:8@?16"
    };
    for (size_t i = 0; i < sizeof(rejects)/sizeof(*rejects); ++i)
        assert(phase25b_parse_encoding(rejects[i]) == PHASE25B_UNSUPPORTED);
}
int main(void) {
    encodings();
    assert(phase25b_create(&runtime) == 0);
    /* Metadata-only on x86_64; these bytes are NEVER called by this test. */
    const uint32_t return7[] = {0x528000e0u, 0xd65f03c0u};
    const uint32_t return9[] = {0x52800120u, 0xd65f03c0u};
    assert(phase25b_publish_image(runtime, return7, sizeof(return7), &image0) == 0);
    assert(phase25b_publish_image(runtime, return9, sizeof(return9), &image1) == 0);
    assert(phase25b_register_class(runtime, "Root", NULL) == 0);
    assert(phase25b_register_class(runtime, "Child", "Root") == 0);
    assert(phase25b_register_class(runtime, "Leaf", "Child") == 0);
    assert(phase25b_register_class(runtime, "Bad", "Missing") != 0);
    assert(phase25b_register_class(runtime, "Root", NULL) != 0);
    assert(phase25b_class_token(runtime, "Root", 0, &root_token) == 0);
    assert(phase25b_class_token(runtime, "Child", 0, &child_token) == 0);
    assert(phase25b_class_token(runtime, "Leaf", 0, &leaf_token) == 0);
    assert(phase25b_selector(runtime, "add:", &add_selector) == 0);
    assert(phase25b_selector(runtime, "kind", &class_selector) == 0);
    assert(phase25b_method(runtime, "Root", 0, "add:", "Q24@0:8Q16", image0, 0, 0) == 0);
    assert(phase25b_method(runtime, "Root", 1, "kind", "Q16@0:8", image1, 0, 0) == 0);
    assert(phase25b_method(runtime, "Root", 0, "bad", "Q24@0:8d16", image0, 0, 0) != 0);
    assert(phase25b_method(runtime, "Root", 0, "add:", "Q24@0:8Q16", image0, 0, 0) != 0);
    assert(phase25b_method(runtime, "Root", 0, "missing:", "Q24@0:8Q16", image0, 0, 1) != 0);
    obj = phase25b_alloc(runtime, "Leaf");
    assert(obj != NULL);
    assert(phase25b_set_active(runtime) == 0);
    phase25b_ticket ticket;
    assert(phase25b_prepare(obj, add_selector, &ticket, 0) != NULL);
    assert(ticket.receiver == obj && ticket.image_slot_plus_one == image0+1);
    assert(phase25b_release(runtime, obj) == -2);
    assert(phase25b_retire_image(runtime, image0) != 0);
    assert(phase25b_set_active(NULL) != 0); /* live call pin prevents swap */
    phase25b_finish(&ticket);
    assert(phase25b_prepare(obj, add_selector, &ticket, 0) != NULL);
    assert(ticket.image_slot_plus_one == image0+1); /* cache hit */
    phase25b_finish(&ticket);
    phase25b_super sup = {obj, child_token};
    assert(phase25b_prepare(&sup, add_selector, &ticket, 1) != NULL);
    assert(ticket.receiver == obj);
    phase25b_finish(&ticket);
    sup.current_class = root_token;
    assert(phase25b_prepare(&sup, add_selector, &ticket, 1) == NULL);
    assert(phase25b_last_status() == PHASE25B_BAD_SUPER);
    sup.current_class = leaf_token;
    assert(phase25b_prepare(&sup, add_selector, &ticket, 1) != NULL);
    phase25b_finish(&ticket);
    assert(phase25b_prepare(root_token, class_selector, &ticket, 0) != NULL);
    phase25b_finish(&ticket);
    assert(phase25b_prepare(NULL, add_selector, &ticket, 0) == NULL);
    assert(phase25b_last_status() == PHASE25B_NIL);
    assert(phase25b_prepare((void *)(uintptr_t)0x1234u, add_selector, &ticket, 0) == NULL);
    assert(phase25b_last_status() == PHASE25B_BAD_RECEIVER);
    assert(phase25b_prepare(obj, (phase25b_sel)(uintptr_t)0x1u, &ticket, 0) == NULL);
    assert(phase25b_last_status() == PHASE25B_BAD_SELECTOR);
    pthread_t threads[8];
    for (size_t i = 0; i < 8; ++i)
        assert(pthread_create(&threads[i], NULL, worker, NULL) == 0);
    for (size_t i = 0; i < 1000; ++i) {
        assert(phase25b_method(runtime, "Root", 0, "add:", "Q24@0:8Q16",
                               i & 1u ? image0 : image1, 0, 1) == 0);
    }
    for (size_t i = 0; i < 8; ++i)
        assert(pthread_join(threads[i], NULL) == 0);
    phase25b_stats stats;
    phase25b_statistics(runtime, &stats);
    assert(stats.sends >= 240000 && stats.hits > 0 && stats.misses > 0);
    assert(stats.in_flight == 0 && stats.live_objects == 1);
    assert(phase25b_release(runtime, obj) == 0);
    assert(phase25b_release(runtime, obj) != 0);
    assert(phase25b_set_active(NULL) == 0);
    assert(phase25b_destroy(runtime) == 0);
    printf("phase25b x86 metadata/concurrency: %llu pinned resolutions, hits=%llu misses=%llu\n",
           (unsigned long long)stats.sends,
           (unsigned long long)stats.hits, (unsigned long long)stats.misses);
    return 0;
}
