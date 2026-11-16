/* Native Mach-O compiler TLS: independent initialized and zero-filled data. */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static _Thread_local volatile uint64_t value = 0x13579bdfu;
static _Thread_local volatile unsigned char zero[16];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static const volatile void *addresses[4];
static unsigned arrived, errors, checks;
static int release;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)

static void *worker(void *argument) {
    uintptr_t index = (uintptr_t)argument;
    unsigned invalid = value != 0x13579bdfu;
    for (unsigned i = 0; i < sizeof(zero); ++i) invalid += zero[i] != 0;
    value = 100 + index;
    zero[0] = (unsigned char)(index + 1);
    zero[15] = (unsigned char)(index + 10);
    pthread_mutex_lock(&lock);
    errors += invalid;
    addresses[index] = &value;
    ++arrived;
    pthread_cond_broadcast(&changed);
    while (!release) pthread_cond_wait(&changed, &lock);
    errors += value != 100 + index || zero[0] != index + 1 || zero[15] != index + 10;
    pthread_mutex_unlock(&lock);
    return NULL;
}

int main(void) {
    alarm(10);
    CHECK(value == 0x13579bdfu && zero[0] == 0 && zero[15] == 0);
    value = 9; zero[0] = 88;
    for (unsigned round = 0; round < 2; ++round) {
        pthread_t threads[4];
        arrived = 0; release = 0;
        for (uintptr_t i = 0; i < 4; ++i) CHECK(pthread_create(&threads[i], NULL, worker, (void *)i) == 0);
        CHECK(pthread_mutex_lock(&lock) == 0);
        while (arrived != 4) CHECK(pthread_cond_wait(&changed, &lock) == 0);
        CHECK(errors == 0 && value == 9 && zero[0] == 88);
        for (unsigned i = 0; i < 4; ++i) {
            CHECK(addresses[i] != &value);
            for (unsigned j = 0; j < i; ++j) CHECK(addresses[i] != addresses[j]);
        }
        release = 1;
        CHECK(pthread_cond_broadcast(&changed) == 0);
        CHECK(pthread_mutex_unlock(&lock) == 0);
        for (unsigned i = 0; i < 4; ++i) CHECK(pthread_join(threads[i], NULL) == 0);
    }
    CHECK(errors == 0 && value == 9 && zero[0] == 88);
    printf("DARWIN_TLV_OK threads=8 checks=%u isolation=ok zero_fill=ok reinit=ok\n", checks);
    return 0;
}
