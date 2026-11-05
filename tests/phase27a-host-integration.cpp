#include "ios27/phase27a_startup.h"
#include "ios27/phase25c_lifetime.h"
#include "ios27/phase25d_lifecycle.hpp"
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

using ios27::phase25d::Action;
using ios27::phase25d::Engine;
using ios27::phase25d::Event;
using ios27::phase25d::Lease;
using ios27::phase25d::Status;

struct Node { void *isa; void *child; unsigned value; };
struct Integration {
    std::mutex mutex;
    std::vector<unsigned> callbacks;
    std::atomic<unsigned> deallocated{0};
    unsigned entered = 0;
    int class_identity = 0;
    ios27_phase25c_runtime *ownership = nullptr;
    Engine lifecycle;
    Integration() : lifecycle([this](const Event &event) {
        std::lock_guard<std::mutex> lock(mutex);
        unsigned prefix = event.action == Action::class_load ? 100u :
                          event.action == Action::category_load ? 200u : 300u;
        callbacks.push_back(prefix + (event.action == Action::category_load ?
                         event.category_id : event.class_id));
        return Status::ok;
    }) {}
};

static void finalize(ios27_phase25c_runtime *, void *, void *ctx) {
    static_cast<Integration *>(ctx)->deallocated.fetch_add(1u);
}
static int activate(void *ctx, const ios27_phase27a_image *image) {
    auto &in = *static_cast<Integration *>(ctx);
    return in.lifecycle.load_image(image->id) == Status::ok ? 0 : -1;
}
static int rollback(void *ctx, const ios27_phase27a_image *image) {
    auto &in = *static_cast<Integration *>(ctx);
    return in.lifecycle.retire_image(image->id) == Status::ok ? 0 : -1;
}
static int run_app_adapter(void *ctx, const ios27_phase27a_image *app,
                           const ios27_phase27a_frame *frame, int *exit_code) {
    auto &in = *static_cast<Integration *>(ctx);
    assert(app->id == 3 && frame->argc == 2);
    assert(!std::strcmp(frame->argv[1], "fixture"));
    assert(!std::strcmp(frame->env[0], "MODE=phase27a"));
    assert(!std::strcmp(frame->apple[0], "exec=/App/fixture"));
    std::vector<std::thread> workers;
    std::atomic<unsigned> failures{0};
    for (unsigned i = 0; i < 6; ++i) {
        workers.emplace_back([&] {
            for (unsigned k = 0; k < 64; ++k)
                if (in.lifecycle.first_message(2) != Status::ok)
                    ++failures;
        });
    }
    for (auto &thread : workers) thread.join();
    assert(failures == 0);
    Lease lease;
    assert(in.lifecycle.begin_method(2, lease) == Status::ok);
    const ios27_phase25c_class cls = {
        &in.class_identity, 27u, sizeof(Node), alignof(Node),
        offsetof(Node, child), sizeof(void *)
    };
    void *object = nullptr;
    assert(ios27_phase25c_alloc(in.ownership, &cls, finalize, &in, &object) == 0);
    assert(ios27_phase25c_identity(in.ownership, object, cls.isa,
                                   cls.image_generation) == 0);
    auto prior = ios27_phase25c_scope_enter(in.ownership);
    void *weak = nullptr;
    assert(objc_storeWeak(&weak, object) == object);
    assert(objc_loadWeakRetained(&weak) == object);
    objc_release(object); /* release retained weak load */
    assert(ios27_phase25c_pin(in.ownership, object) == 0);
    objc_release(object); /* terminal; deallocation waits for dispatch pin */
    assert(weak == nullptr && in.deallocated.load() == 0u);
    assert(ios27_phase25c_unpin(in.ownership, object) == 0);
    assert(in.deallocated.load() == 1u);
    objc_destroyWeak(&weak);
    void *pool = objc_autoreleasePoolPush();
    void *pooled = nullptr;
    assert(ios27_phase25c_alloc(in.ownership, &cls, finalize, &in, &pooled) == 0);
    assert(objc_autorelease(pooled) == pooled);
    objc_autoreleasePoolPop(pool);
    assert(in.deallocated.load() == 2u);
    ios27_phase25c_scope_leave(prior);
    assert(ios27_phase25c_thread_detach(in.ownership) == 0);
    assert(in.lifecycle.end_method(lease) == Status::ok);
    ios27_phase25c_stats stats{};
    ios27_phase25c_get_stats(in.ownership, &stats);
    assert(stats.live_objects == 0 && stats.weak_slots == 0 &&
           stats.deallocations == 2);
    ++in.entered;
    *exit_code = 0;
    return 0;
}

int main() {
    Integration ctx;
    assert(ios27_phase25c_create(&ctx.ownership) == 0);
    assert(ctx.lifecycle.register_image({1,{}}) == Status::ok);
    assert(ctx.lifecycle.register_image({2,{1}}) == Status::ok);
    assert(ctx.lifecycle.register_image({3,{2}}) == Status::ok);
    assert(ctx.lifecycle.register_class({1,1,ios27::phase25d::kNoClass,1,11}) == Status::ok);
    assert(ctx.lifecycle.register_class({2,2,1,2,12}) == Status::ok);
    assert(ctx.lifecycle.register_category({4,2,1,4}) == Status::ok);
    for (unsigned id = 1; id <= 3; ++id)
        assert(ctx.lifecycle.mark_macho_ready(id) == Status::ok);
    const uint32_t derived_dep[] = {1}, app_dep[] = {2};
    const ios27_phase27a_image images[] = {
        {3,"app",app_dep,1,0x1000,0x200,0x1100,1,1,0},
        {2,"derived",derived_dep,1,0x1000,0x200,0,0,1,0},
        {1,"root",nullptr,0,0x1000,0x200,0,0,1,0}
    };
    const char *const argv[] = {"App","fixture"};
    const char *const env[] = {"MODE=phase27a"};
    const char *const apple[] = {"exec=/App/fixture"};
    ios27_phase27a_request request = {
        images,3,{2,argv,1,env,1,apple},
        {activate,rollback,run_app_adapter},&ctx
    };
    ios27_phase27a_result result{};
    assert(ios27_phase27a_launch(&request,&result) == IOS27_PHASE27A_OK);
    assert(result.activated == 3 && result.rolled_back == 3 && ctx.entered == 1);
    const std::vector<unsigned> expected{101,102,204,301,302};
    assert(ctx.callbacks == expected);
    assert(ctx.lifecycle.statistics().pins == 0);
    assert(ios27_phase25c_destroy(&ctx.ownership) == 0);
    assert(ctx.ownership == nullptr);
    std::puts("phase27a: startup + phase25c ARC + phase25d lifecycle host-adapter integration passed; zero Darwin guest instructions claimed");
    return 0;
}