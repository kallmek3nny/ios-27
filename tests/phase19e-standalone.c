#include "ios27/objc_metadata.h"
#include "ios27/objc_bridge.h"
#include "ios27/objc_runtime.h"
#include "ios27/libsystem_user.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__aarch64__) && defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

static int roots_initialized, children_initialized, retry;
static int initialize_root(void) { ++roots_initialized; return 0; }
static int initialize_child(void) {
    ++children_initialized;
    if (retry) { retry = 0; return -1; }
    return 0;
}
static intptr_t root_number(void *self, const char *sel) {
    (void)sel;
    return *(intptr_t *)((uint8_t *)self + sizeof(void *));
}
static intptr_t child_number(void *self, const char *sel) {
    return root_number(self, sel) + 5;
}
static intptr_t constant(void *self, const char *sel) {
    (void)self;
    return strcmp(sel, "status") == 0 ? 83 : -1;
}
static void runtime_tests(void) {
    ios27_objc_runtime *r = NULL;
    ios27_objc_method_spec root_methods[] = {{"number", root_number},
                                             {"status", constant}};
    ios27_objc_method_spec child_methods[] = {{"number", child_number}};
    ios27_objc_class_spec initial[] = {
        {"Child", "Root", 24, initialize_child, child_methods, 1},
        {"Root", NULL, 16, initialize_root, root_methods, 2}
    };
    ios27_objc_class_spec invalid[] = {
        {"CycleA", "CycleB", 16, NULL, NULL, 0},
        {"CycleB", "CycleA", 16, NULL, NULL, 0}
    };
    size_t cls, sel, obj, id;
    intptr_t value = 0;
    uint16_t token, same;
    void *objects[IOS27_OBJC_MAX_OBJECTS];
    assert(ios27_objc_runtime_create(&r) == 0 && r);
    assert(ios27_objc_register_classes(r, invalid, 2) != 0);
    ios27_objc_counts(r, &cls, &sel, &obj);
    assert(cls == 0 && sel == 0 && obj == 0);
    assert(ios27_objc_register_classes(r, initial, 2) == 0);
    assert(ios27_objc_register_classes(r, initial, 2) != 0);
    assert(ios27_objc_class_index(r, "Child", &id) == 0);
    assert(ios27_objc_class_index(r, "Missing", &id) != 0);
    assert(ios27_objc_selector(r, "number", &token) == 0);
    assert(ios27_objc_selector(r, "number", &same) == 0 && same == token);
    retry = 1;
    assert(ios27_objc_alloc(r, "Child") == NULL);
    assert(roots_initialized == 1 && children_initialized == 1);
    objects[0] = ios27_objc_alloc(r, "Child");
    assert(objects[0] && roots_initialized == 1 && children_initialized == 2);
    *(intptr_t *)((uint8_t *)objects[0] + sizeof(void *)) = 37;
    assert(ios27_objc_send(r, objects[0], "number", &value) == 0 && value == 42);
    assert(ios27_objc_send(r, objects[0], "status", &value) == 0 && value == 83);
    assert(ios27_objc_send(r, objects[0], "unknown", &value) != 0);
    assert(ios27_objc_send(r, (void *)(uintptr_t)12, "number", &value) != 0);
    for (size_t i = 1; i < IOS27_OBJC_MAX_OBJECTS; ++i) {
        objects[i] = ios27_objc_alloc(r, "Root");
        assert(objects[i]);
    }
    assert(ios27_objc_alloc(r, "Root") == NULL);
    ios27_objc_counts(r, &cls, &sel, &obj);
    assert(cls == 2 && sel == 2 && obj == IOS27_OBJC_MAX_OBJECTS);
    for (size_t i = 0; i < IOS27_OBJC_MAX_OBJECTS; ++i) {
        assert(ios27_objc_release(r, objects[i]) == 0);
        assert(ios27_objc_release(r, objects[i]) != 0);
    }
    ios27_objc_counts(r, NULL, NULL, &obj);
    assert(obj == 0);
    {
        uint32_t state = 0x19e5u;
        for (size_t iteration = 0; iteration < 4000; ++iteration) {
            state = state * 1664525u + 1013904223u;
            void *temporary = ios27_objc_alloc(r, (state & 1u) ? "Root" : "Child");
            assert(temporary);
            assert(ios27_objc_send(r, temporary, "status", &value) == 0 && value == 83);
            assert(ios27_objc_release(r, temporary) == 0);
        }
    }
    ios27_objc_runtime_destroy(r);
}
static void services(void) {
    ios27_libsystem *s = NULL;
    uint64_t args[6] = {0};
    ios27_libsystem_result result;
    char text[64] = {0};
    void *heap[IOS27_LIBSYSTEM_MAX_ALLOCS];
    size_t n, allocations;
    assert(ios27_libsystem_create(&s, 123) == 0);
    args[0] = 1;
    args[1] = IOS27_LIBSYSTEM_GUEST_BYTES - 4u;
    args[2] = 4;
    memcpy(ios27_libsystem_guest(s, (size_t)args[1], 4), "test", 4);
    result = ios27_libsystem_syscall(s, IOS27_DARWIN_CLASS_UNIX | IOS27_DARWIN_SYS_WRITE, args);
    assert(result.darwin_errno == 0 && result.value == 4);
    assert(ios27_libsystem_output(s, 1, text, sizeof(text)) == 4);
    assert(memcmp(text, "test", 4) == 0);
    args[2] = 5;
    result = ios27_libsystem_syscall(s, IOS27_DARWIN_CLASS_UNIX | IOS27_DARWIN_SYS_WRITE, args);
    assert(result.darwin_errno == IOS27_DARWIN_EFAULT);
    assert(ios27_libsystem_output(s, 1, text, sizeof(text)) == 4);
    assert(ios27_libsystem_stdin(s, "abc", 3) == 0);
    args[0] = 0; args[1] = 0; args[2] = 5;
    result = ios27_libsystem_syscall(s, IOS27_DARWIN_CLASS_UNIX | IOS27_DARWIN_SYS_READ, args);
    assert(result.value == 3 && result.darwin_errno == 0);
    assert(memcmp(ios27_libsystem_guest(s, 0, 3), "abc", 3) == 0);
    assert(ios27_libsystem_syscall(s, IOS27_DARWIN_CLASS_UNIX | IOS27_DARWIN_SYS_GETPID, args).value == 123);
    assert(ios27_libsystem_syscall(s, IOS27_DARWIN_CLASS_UNIX | 999u, args).darwin_errno == IOS27_DARWIN_ENOSYS);
    assert(ios27_libsystem_syscall(s, 0x01000000u, args).darwin_errno == IOS27_DARWIN_ENOSYS);
    args[0] = 1;
    assert(ios27_libsystem_syscall(s, IOS27_DARWIN_CLASS_UNIX | IOS27_DARWIN_SYS_CLOSE, args).value == 0);
    assert(ios27_libsystem_syscall(s, IOS27_DARWIN_CLASS_UNIX | IOS27_DARWIN_SYS_CLOSE, args).darwin_errno == IOS27_DARWIN_EBADF);
    assert(ios27_libsystem_malloc(s, SIZE_MAX) == NULL);
    for (size_t i = 0; i < IOS27_LIBSYSTEM_MAX_ALLOCS; ++i) {
        heap[i] = ios27_libsystem_malloc(s, 20);
        assert(heap[i]);
    }
    assert(ios27_libsystem_malloc(s, 1) == NULL);
    ios27_libsystem_heap_state(s, &n, &allocations);
    assert(n == IOS27_LIBSYSTEM_MAX_ALLOCS * 20 && allocations == IOS27_LIBSYSTEM_MAX_ALLOCS);
    for (size_t i = 0; i < IOS27_LIBSYSTEM_MAX_ALLOCS; ++i) {
        assert(ios27_libsystem_free(s, heap[i]) == 0);
        assert(ios27_libsystem_free(s, heap[i]) != 0);
    }
    ios27_libsystem_heap_state(s, &n, &allocations);
    assert(n == 0 && allocations == 0);
    ios27_libsystem_destroy(s);
}
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static void put32(uint8_t *p, uint32_t n) {
    for (unsigned j = 0; j < 4; ++j) p[j] = (uint8_t)(n >> (j * 8u));
}
static void reject(uint8_t *copy, size_t size) {
    ios27_objc_metadata meta;
    char err[128];
    assert(ios27_objc_metadata_inspect(copy, size, &meta, err, sizeof(err)) != 0);
    assert(err[0]);
    assert(meta.class_count == 0);
}
static intptr_t compiled_init(void *self, const char *selector) {
    (void)selector;
    return (intptr_t)self;
}
static intptr_t compiled_answer(void *self, const char *selector) {
    (void)self;
    (void)selector;
    return 42;
}
static ios27_objc_imp compiled_resolver(void *context, const char *name,
                                       const char *selector) {
    (void)context;
    if (strcmp(selector, "init") == 0 && strcmp(name, "Root") == 0)
        return compiled_init;
    if (strcmp(selector, "answer") == 0 && strcmp(name, "Root") == 0)
        return compiled_answer;
    if (strcmp(selector, "number") != 0) return NULL;
    return strcmp(name, "Child") == 0 ? child_number : root_number;
}
static void compiled(const char *path) {
    FILE *f = fopen(path, "rb");
    long size_long;
    size_t size;
    uint8_t *data, *copy;
    ios27_objc_metadata *m = malloc(sizeof(*m));
    char err[128] = {0};
    size_t pos = 32u, section_cmd = 0u;
    assert(f && m);
    assert(fseek(f, 0, SEEK_END) == 0);
    size_long = ftell(f);
    assert(size_long > 32 && size_long <= 2 * 1024 * 1024);
    size = (size_t)size_long;
    assert(fseek(f, 0, SEEK_SET) == 0);
    data = malloc(size);
    copy = malloc(size);
    assert(data && copy && fread(data, 1, size, f) == size);
    assert(fclose(f) == 0);
    assert(ios27_objc_metadata_inspect(data, size, m, err, sizeof(err)) == 0);
    assert(m->class_count == 2 && m->selector_count == 3);
    assert(strcmp(m->classes[0].name, "Root") == 0);
    assert(strcmp(m->classes[1].name, "Child") == 0);
    assert(strcmp(m->classes[1].superclass, "Root") == 0);
    assert(m->classes[0].method_count == 3);
    assert(m->classes[1].method_count == 1);
    assert(m->classes[0].instance_size <= m->classes[1].instance_size);
    assert(strcmp(m->classes[0].methods[0].selector, "init") == 0);
    assert(strcmp(m->classes[0].methods[1].selector, "answer") == 0 ||
           strcmp(m->classes[0].methods[2].selector, "answer") == 0);
    assert(strcmp(m->classes[1].methods[0].selector, "number") == 0);
    {
        ios27_objc_runtime *rt = NULL;
        intptr_t val = 0;
        void *obj;
        assert(ios27_objc_runtime_create(&rt) == 0);
        assert(ios27_objc_register_metadata(rt, m, compiled_resolver, NULL) == 0);
        obj = ios27_objc_alloc(rt, "Child");
        assert(obj);
        *(intptr_t *)((uint8_t *)obj + sizeof(void *)) = 37;
        assert(ios27_objc_send(rt, obj, "number", &val) == 0 && val == 42);
        assert(ios27_objc_send(rt, obj, "answer", &val) == 0 && val == 42);
        assert(ios27_objc_release(rt, obj) == 0);
        ios27_objc_runtime_destroy(rt);
    }
#if defined(__aarch64__) && defined(__linux__)
    /* Execute *real Clang-generated ObjC method instructions*, but only
     * an original, relocation-free answer method. No guest runtime magic. */
    {
        uint64_t entry = UINT64_MAX;
        uint64_t text_addr = 0, text_len = 0;
        uint32_t text_off = 0;
        size_t cursor = 32;
        long page = sysconf(_SC_PAGESIZE);
        uint8_t *mapping;
        intptr_t (*native_method)(void *, const char *);
        for (uint32_t i = 0; i < m->classes[0].method_count; ++i)
            if (!strcmp(m->classes[0].methods[i].selector, "answer"))
                entry = m->classes[0].methods[i].implementation_vmaddr;
        for (uint32_t i = 0; i < u32(data + 16); ++i) {
            uint32_t cmd = u32(data + cursor);
            uint32_t n = u32(data + cursor + 4);
            if (cmd == 0x19u) {
                uint32_t count = u32(data + cursor + 64);
                for (uint32_t j = 0; j < count; ++j) {
                    const uint8_t *section = data + cursor + 72 + j * 80;
                    if (memcmp(section, "__text", 7) == 0) {
                        text_addr = ((uint64_t)u32(section + 36) << 32) |
                                     u32(section + 32);
                        text_len = ((uint64_t)u32(section + 44) << 32) |
                                    u32(section + 40);
                        text_off = u32(section + 48);
                    }
                }
            }
            cursor += n;
        }
        assert(entry != UINT64_MAX && page >= 4096 &&
               entry >= text_addr && entry - text_addr + 8 <= text_len &&
               text_len <= (uint64_t)page && text_off + text_len <= size);
        mapping = mmap(NULL, (size_t)page, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(mapping != MAP_FAILED);
        memcpy(mapping, data + text_off, (size_t)text_len);
        __builtin___clear_cache((char *)mapping, (char *)mapping + text_len);
        assert(mprotect(mapping, (size_t)page, PROT_READ | PROT_EXEC) == 0);
        {
            uintptr_t address = (uintptr_t)(mapping + (size_t)(entry - text_addr));
            memcpy(&native_method, &address, sizeof(native_method));
        }
        assert(native_method(NULL, NULL) == 42);
        assert(munmap(mapping, (size_t)page) == 0);
    }
#endif
    memcpy(copy, data, size);
    put32(copy, 0); reject(copy, size);
    memcpy(copy, data, size);
    put32(copy + 12, 2); reject(copy, size);
    memcpy(copy, data, size);
    put32(copy + 16, UINT32_MAX); reject(copy, size);
    memcpy(copy, data, size);
    put32(copy + 20, UINT32_MAX); reject(copy, size);
    memcpy(copy, data, size);
    for (uint32_t i = 0; i < u32(data + 16); ++i) {
        uint32_t cmd = u32(data + pos), n = u32(data + pos + 4);
        if (cmd == 0x19u) section_cmd = pos;
        pos += n;
    }
    assert(section_cmd);
    memcpy(copy, data, size);
    put32(copy + section_cmd + 64, UINT32_MAX); reject(copy, size);
    memcpy(copy, data, size);
    put32(copy + section_cmd + 72 + 48, UINT32_MAX); reject(copy, size);
    for (size_t cut = 0; cut < 40; cut++) {
        assert(ios27_objc_metadata_inspect(data, cut, m, err, sizeof(err)) != 0);
    }
    assert(ios27_objc_metadata_inspect(data, size, m, err, sizeof(err)) == 0);
    free(m); free(copy); free(data);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    runtime_tests();
    services();
    compiled(argv[1]);
    puts("phase19e: native runtime, libSystem service, compiled ObjC metadata passed");
    return 0;
}
