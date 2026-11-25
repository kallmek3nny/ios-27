/* Native worker-pool contract: all six QoS classes, overcommit, and reuse.
 * This measures execution independently of the advertised feature mask. */
#define __PTHREAD_EXPOSE_INTERNALS__ 1
#include <pthread.h>
#include <pthread/workqueue_private.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static pthread_key_t key;
static const qos_class_t classes[] = {
    QOS_CLASS_MAINTENANCE, QOS_CLASS_BACKGROUND, QOS_CLASS_UTILITY,
    QOS_CLASS_DEFAULT, QOS_CLASS_USER_INITIATED, QOS_CLASS_USER_INTERACTIVE
};
static unsigned counts[6][2], completed, errors, reused, checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)

static void worker(pthread_priority_t priority) {
    int relative = 1;
    unsigned long flags = 0;
    qos_class_t qos = _pthread_qos_class_decode(priority, &relative, &flags);
    unsigned group = 0;
    while (group < 6 && classes[group] != qos) ++group;
    unsigned overcommit = (flags & _PTHREAD_PRIORITY_OVERCOMMIT_FLAG) != 0;
    uintptr_t previous = (uintptr_t)pthread_getspecific(key);
    int invalid = pthread_setspecific(key, (void *)(previous + 1)) != 0 ||
                  group == 6 || relative != 0 || qos_class_self() != qos || pthread_main_np();
    pthread_mutex_lock(&lock);
    if (invalid) ++errors;
    if (group < 6) ++counts[group][overcommit];
    if (previous) ++reused;
    ++completed;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
}

int main(void) {
    const unsigned waves = 16, per_request = 2, per_wave = 6 * 2 * per_request;
    CHECK(pthread_key_create(&key, NULL) == 0);
    CHECK(_pthread_workqueue_init(worker, 0, 0) == 0);
    int advertised = _pthread_workqueue_supported();
    for (unsigned wave = 0; wave < waves; ++wave) {
        for (unsigned group = 0; group < 6; ++group) {
            for (unsigned overcommit = 0; overcommit < 2; ++overcommit) {
                pthread_priority_t priority = _pthread_qos_class_encode(classes[group], 0,
                    overcommit ? _PTHREAD_PRIORITY_OVERCOMMIT_FLAG : 0);
                CHECK(_pthread_workqueue_addthreads(per_request, priority) == 0);
            }
        }
        struct timespec deadline = {.tv_sec = time(NULL) + 10, .tv_nsec = 0};
        CHECK(pthread_mutex_lock(&lock) == 0);
        while (completed < (wave + 1) * per_wave) {
            CHECK(pthread_cond_timedwait(&changed, &lock, &deadline) == 0);
        }
        CHECK(completed == (wave + 1) * per_wave && errors == 0);
        CHECK(pthread_mutex_unlock(&lock) == 0);
    }
    for (unsigned group = 0; group < 6; ++group)
        for (unsigned overcommit = 0; overcommit < 2; ++overcommit)
            CHECK(counts[group][overcommit] == waves * per_request);
    printf("DARWIN_WORKQUEUE_OK callbacks=%u reused=%u feature_mask=0x%x checks=%u\n",
           completed, reused, advertised, checks);
    return 0;
}
