#include "ios27/phase27a_startup.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct test_context {
    uint32_t activated[8], retired[8];
    size_t add_count, remove_count;
    uint32_t fail_on;
    uint32_t rollback_fail_on;
    int entry_failure, guest_exit;
    unsigned enters;
};

static int activate(void *data, const ios27_phase27a_image *im) {
    struct test_context *ctx = data;
    assert(ctx->add_count < 8);
    ctx->activated[ctx->add_count++] = im->id;
    return im->id == ctx->fail_on ? -1 : 0;
}
static int undo(void *data, const ios27_phase27a_image *im) {
    struct test_context *ctx = data;
    assert(ctx->remove_count < 8);
    ctx->retired[ctx->remove_count++] = im->id;
    return im->id == ctx->rollback_fail_on ? -1 : 0;
}
static int entry(void *data, const ios27_phase27a_image *im,
                 const ios27_phase27a_frame *frame, int *code) {
    struct test_context *ctx = data;
    ++ctx->enters;
    assert(im->id == 3 && im->has_main && im->entry_fileoff == 0x1100);
    assert(frame->argc == 2 && frame->argv[2] == NULL);
    assert(!strcmp(frame->argv[0], "phase27a") &&
           !strcmp(frame->argv[1], "ok"));
    assert(!strcmp(frame->env[0], "MODE=phase27a") && frame->env[1] == NULL);
    assert(!strcmp(frame->apple[0], "exec=/App/phase27a") &&
           frame->apple[1] == NULL);
    /* Adapter asserts argv, env and apple are independently copied. */
    assert(frame->argv[0] != (void *)im->name);
    *code = ctx->guest_exit;
    return ctx->entry_failure;
}

static const uint32_t leaf_deps[] = {1};
static const uint32_t app_deps[] = {2, 1};
static const char *const argv_data[] = {"phase27a", "ok"};
static const char *const env_data[] = {"MODE=phase27a"};
static const char *const apple_data[] = {"exec=/App/phase27a"};
static ios27_phase27a_image graph_images[] = {
    {.id=3, .name="app", .dependencies=app_deps, .dependency_count=2,
     .text_fileoff=0x1000,.text_filesize=0x200,.entry_fileoff=0x1100,
     .has_main=1,.text_executable=1,.text_writable=0},
    {.id=2,.name="derived",.dependencies=leaf_deps,.dependency_count=1,
     .text_fileoff=0x1000,.text_filesize=0x200,.text_executable=1},
    {.id=1,.name="root",.text_fileoff=0x1000,.text_filesize=0x200,
     .text_executable=1}
};

static ios27_phase27a_request make_request(struct test_context *ctx) {
    ios27_phase27a_request req = {0};
    req.images = graph_images;
    req.image_count = 3;
    req.inputs = (ios27_phase27a_inputs){2, argv_data, 1, env_data, 1, apple_data};
    req.hooks = (ios27_phase27a_hooks){activate, undo, entry};
    req.context = ctx;
    return req;
}

static void check_order(const struct test_context *ctx, unsigned active) {
    const uint32_t expected_add[] = {1,2,3};
    const uint32_t expected_remove[] = {3,2,1};
    assert(ctx->add_count == active);
    assert(ctx->remove_count == active);
    for (unsigned i = 0; i < active; ++i) {
        assert(ctx->activated[i] == expected_add[i]);
        assert(ctx->retired[i] == expected_remove[3u-active+i]);
    }
}

static void success_and_failures(void) {
    struct test_context ctx = {0};
    ios27_phase27a_request req = make_request(&ctx);
    ios27_phase27a_result result = {0};
    assert(ios27_phase27a_launch(&req, &result) == IOS27_PHASE27A_OK);
    assert(result.status == IOS27_PHASE27A_OK && result.cause == IOS27_PHASE27A_OK);
    assert(result.activated == 3 && result.rolled_back == 3);
    assert(result.main_invocations == 1 && ctx.enters == 1);
    check_order(&ctx, 3);

    memset(&ctx, 0, sizeof(ctx));
    ctx.fail_on = 2;
    assert(ios27_phase27a_launch(&req, &result) == IOS27_PHASE27A_ACTIVATION_FAILED);
    assert(result.failed_image == 2 && result.main_invocations == 0);
    check_order(&ctx, 2);

    memset(&ctx, 0, sizeof(ctx));
    ctx.fail_on = 2;
    ctx.rollback_fail_on = 1;
    assert(ios27_phase27a_launch(&req, &result) == IOS27_PHASE27A_ROLLBACK_FAILED);
    assert(result.cause == IOS27_PHASE27A_ACTIVATION_FAILED);
    check_order(&ctx, 2);

    memset(&ctx, 0, sizeof(ctx));
    ctx.entry_failure = 1;
    assert(ios27_phase27a_launch(&req, &result) == IOS27_PHASE27A_ENTRY_FAILED);
    check_order(&ctx, 3);

    memset(&ctx, 0, sizeof(ctx));
    ctx.guest_exit = 41;
    assert(ios27_phase27a_launch(&req, &result) == IOS27_PHASE27A_GUEST_FAILED);
    assert(result.guest_exit_code == 41);
    check_order(&ctx, 3);
}

static void invalid_input_tests(void) {
    struct test_context ctx = {0};
    ios27_phase27a_request req = make_request(&ctx);
    ios27_phase27a_result result = {0};
    ios27_phase27a_image copy[3];
    memcpy(copy, graph_images, sizeof(copy));
    req.images = copy;
#define BAD(expected) do { \
    assert(ios27_phase27a_launch(&req,&result) == (expected)); \
    assert(ctx.add_count == 0 && ctx.remove_count == 0 && ctx.enters == 0); \
    assert(result.activated == 0 && result.main_invocations == 0); \
} while (0)
    copy[0].entry_fileoff = 0x1200;
    BAD(IOS27_PHASE27A_UNSUPPORTED);
    copy[0].entry_fileoff = 0x1102;
    BAD(IOS27_PHASE27A_UNSUPPORTED);
    copy[0].entry_fileoff = 0x1100;
    copy[1].text_writable = 1;
    BAD(IOS27_PHASE27A_UNSUPPORTED);
    copy[1].text_writable = 0;
    copy[1].id = 1;
    BAD(IOS27_PHASE27A_INVALID);
    copy[1].id = 2;
    copy[1].name = "root";
    BAD(IOS27_PHASE27A_INVALID);
    copy[1].name = "derived";
    copy[2].dependencies = app_deps;
    copy[2].dependency_count = 2;
    BAD(IOS27_PHASE27A_CYCLE);
    copy[2].dependency_count = 0;
    copy[2].dependencies = NULL;
    copy[0].has_main = 0;
    BAD(IOS27_PHASE27A_UNSUPPORTED);
    copy[0].entry_fileoff = 0;
    BAD(IOS27_PHASE27A_INVALID);
    copy[0].has_main = 1;
    copy[0].entry_fileoff = 0x1100;
    const uint32_t duplicated[] = {2,2};
    copy[0].dependencies = duplicated;
    BAD(IOS27_PHASE27A_INVALID);
    copy[0].dependencies = app_deps;
    const uint32_t missing[] = {99};
    copy[0].dependencies = missing;
    copy[0].dependency_count = 1;
    BAD(IOS27_PHASE27A_INVALID);
    copy[0].dependencies = app_deps;
    copy[0].dependency_count = 2;
    const char *const duplicate_env[] = {"A=1", "A=2"};
    req.inputs.env = duplicate_env;
    req.inputs.envc = 2;
    BAD(IOS27_PHASE27A_INVALID);
    req.inputs.env = env_data;
    req.inputs.envc = 1;
    const char *const bad_env[] = {"NO_EQUALS"};
    req.inputs.env = bad_env;
    BAD(IOS27_PHASE27A_INVALID);
    req.inputs.env = env_data;
    req.inputs.argc = 0;
    BAD(IOS27_PHASE27A_INVALID);
    req.inputs.argc = 2;
    char overlong[IOS27_PHASE27A_MAX_STRING+2u];
    memset(overlong, 'x', sizeof(overlong));
    overlong[sizeof(overlong)-1u] = '\0';
    const char *const bad_argv[] = {"app",overlong};
    req.inputs.argv = bad_argv;
    BAD(IOS27_PHASE27A_INVALID);
    req.inputs.argv = argv_data;
    req.hooks.enter_main = NULL;
    BAD(IOS27_PHASE27A_INVALID);
#undef BAD
}

int main(void) {
    success_and_failures();
    invalid_input_tests();
    puts("phase27a: bounded startup transaction, rollback and negative gates verified (host adapters only)");
    return 0;
}
