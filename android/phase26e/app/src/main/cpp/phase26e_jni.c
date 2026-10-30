#include "ios27/phase26e_host.h"
#include <jni.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct jni_container { phase26e_host *host; phase26e_id scene, window, root; };
static struct jni_container *container(jlong handle) { return (struct jni_container *)(uintptr_t)handle; }
static phase26e_host *host(jlong handle) {
    struct jni_container *c = container(handle);
    return c ? c->host : NULL;
}

JNIEXPORT jlong JNICALL Java_dev_ios27_host_MainActivity_nativeCreate(JNIEnv *env, jclass clazz) {
    (void)env; (void)clazz;
    struct jni_container *c = (struct jni_container *)calloc(1, sizeof(*c));
    if (!c) return 0;
    phase26e_host *h = c->host = phase26e_create();
    if (!h || phase26e_scene_add(h, &c->scene) != P26_OK ||
        phase26e_window_add(h, c->scene, (phase26e_rect){0,0,8192,8192}, &c->window) != P26_OK ||
        phase26e_view_add(h, c->window, 0, (phase26e_rect){0,0,8192,8192}, 0, &c->root) != P26_OK ||
        phase26e_launch(h) != P26_OK) {
        phase26e_destroy(h); free(c); return 0;
    }
    return (jlong)(uintptr_t)c;
}
JNIEXPORT void JNICALL Java_dev_ios27_host_MainActivity_nativeDestroy(JNIEnv *env, jclass clazz, jlong ptr) {
    (void)env; (void)clazz;
    struct jni_container *c = container(ptr);
    if (c) { phase26e_destroy(c->host); free(c); }
}
JNIEXPORT jint JNICALL Java_dev_ios27_host_MainActivity_nativeLifecycle(JNIEnv *env, jclass clazz, jlong ptr, jint action) {
    (void)env; (void)clazz;
    phase26e_host *h = host(ptr);
    if (action == 0) return (jint)phase26e_pause(h);
    if (action == 1) return (jint)phase26e_resume(h);
    if (action == 2) return (jint)phase26e_terminate(h);
    return P26_INVALID;
}
JNIEXPORT jint JNICALL Java_dev_ios27_host_MainActivity_nativeSurface(JNIEnv *env, jclass clazz,
    jlong ptr, jboolean available, jint width, jint height, jint rotation) {
    (void)env; (void)clazz;
    if (width < 0 || height < 0 || rotation < 0) return P26_INVALID;
    return (jint)phase26e_surface(host(ptr), available ? 1 : 0, (uint32_t)width, (uint32_t)height, (uint32_t)rotation);
}
JNIEXPORT jint JNICALL Java_dev_ios27_host_MainActivity_nativePointer(JNIEnv *env, jclass clazz,
    jlong ptr, jint kind, jint pointer, jfloat x, jfloat y) {
    (void)env; (void)clazz;
    return (jint)phase26e_pointer(host(ptr), (phase26e_event_kind)kind, pointer, x, y);
}
JNIEXPORT jint JNICALL Java_dev_ios27_host_MainActivity_nativeKey(JNIEnv *env, jclass clazz,
    jlong ptr, jboolean down, jint key) {
    (void)env; (void)clazz;
    return (jint)phase26e_keyboard(host(ptr), down ? 1 : 0, key);
}
static jint text_input(JNIEnv *env, jlong ptr, jstring text, int compose) {
    if (!text) return P26_INVALID;
    jsize length = (*env)->GetStringLength(env, text);
    if (length < 0 || length > (jsize)PHASE26E_MAX_TEXT) return P26_INVALID;
    const jchar *input = (*env)->GetStringChars(env, text, NULL);
    if (!input) return P26_INVALID;
    char utf8[PHASE26E_MAX_TEXT + 1u];
    size_t n = 0;
    jint result = P26_INVALID;
    for (jsize i = 0; i < length; ++i) {
        uint32_t cp = input[i];
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (++i >= length || input[i] < 0xDC00 || input[i] > 0xDFFF) goto cleanup;
            cp = 0x10000u + ((cp - 0xD800u) << 10u) + (input[i] - 0xDC00u);
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) goto cleanup;
        if (!cp) goto cleanup; /* text payload is NUL-terminated */
        size_t bytes = cp < 0x80u ? 1u : cp < 0x800u ? 2u : cp < 0x10000u ? 3u : 4u;
        if (bytes > PHASE26E_MAX_TEXT - n) goto cleanup;
        if (bytes == 1u) utf8[n++] = (char)cp;
        else {
            if (bytes == 2u) utf8[n++] = (char)(0xC0u | (cp >> 6u));
            else if (bytes == 3u) {
                utf8[n++] = (char)(0xE0u | (cp >> 12u));
                utf8[n++] = (char)(0x80u | ((cp >> 6u) & 63u));
            } else {
                utf8[n++] = (char)(0xF0u | (cp >> 18u));
                utf8[n++] = (char)(0x80u | ((cp >> 12u) & 63u));
                utf8[n++] = (char)(0x80u | ((cp >> 6u) & 63u));
            }
            utf8[n++] = (char)(0x80u | (cp & 63u));
        }
    }
    utf8[n] = '\0';
    result = (jint)(compose ? phase26e_compose(host(ptr), utf8, n) : phase26e_text(host(ptr), utf8, n));
cleanup:
    (*env)->ReleaseStringChars(env, text, input);
    return result;
}

JNIEXPORT jint JNICALL Java_dev_ios27_host_MainActivity_nativeText(JNIEnv *env, jclass clazz,
    jlong ptr, jstring text, jboolean compose) {
    (void)clazz;
    return text_input(env, ptr, text, compose ? 1 : 0);
}
JNIEXPORT jint JNICALL Java_dev_ios27_host_MainActivity_nativeAccessibility(JNIEnv *env, jclass clazz, jlong ptr) {
    (void)env; (void)clazz;
    struct jni_container *c = container(ptr);
    return c ? (jint)phase26e_accessibility_focus(c->host, c->root) : (jint)P26_INVALID;
}
JNIEXPORT void JNICALL Java_dev_ios27_host_MainActivity_nativeFocus(JNIEnv *env, jclass clazz,
    jlong ptr, jboolean focused) {
    (void)env; (void)clazz;
    (void)phase26e_focus(host(ptr), focused ? 1 : 0);
}
JNIEXPORT jlong JNICALL Java_dev_ios27_host_MainActivity_nativeGeneration(JNIEnv *env, jclass clazz, jlong ptr) {
    (void)env; (void)clazz;
    return (jlong)phase26e_get_metrics(host(ptr)).surface_generation;
}
JNIEXPORT jint JNICALL Java_dev_ios27_host_MainActivity_nativePresent(JNIEnv *env, jclass clazz,
    jlong ptr, jlong generation) {
    (void)env; (void)clazz;
    if (generation < 0) return P26_INVALID;
    return (jint)phase26e_present(host(ptr), (uint64_t)generation);
}
JNIEXPORT jint JNICALL Java_dev_ios27_host_MainActivity_nativeRequestFrame(JNIEnv *env, jclass clazz, jlong ptr) {
    (void)env; (void)clazz;
    return (jint)phase26e_request_frame(host(ptr));
}
JNIEXPORT jlongArray JNICALL Java_dev_ios27_host_MainActivity_nativeMetrics(JNIEnv *env, jclass clazz, jlong ptr) {
    (void)clazz;
    phase26e_metrics m = phase26e_get_metrics(host(ptr));
    const jlong values[7] = {(jlong)m.accepted_events, (jlong)m.rejected_events,
        (jlong)m.cancellations, (jlong)m.frames_presented,
        (jlong)m.surface_recreations, (jlong)m.lifecycle_transitions, (jlong)m.dropped_log_events};
    jlongArray result = (*env)->NewLongArray(env, 7);
    if (result) (*env)->SetLongArrayRegion(env, result, 0, 7, values);
    return result;
}