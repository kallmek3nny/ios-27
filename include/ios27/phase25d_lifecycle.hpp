#ifndef IOS27_PHASE25D_LIFECYCLE_HPP
#define IOS27_PHASE25D_LIFECYCLE_HPP
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
namespace ios27::phase25d {
enum class Status { ok, invalid, duplicate, not_ready, cycle, reentrant, deadlock, timeout, callback_failed, failed, busy, retired, stale, unsupported };
enum class State { uninitialized, initializing, initialized, failed, retiring };
enum class Action { class_load, category_load, class_initialize };
struct Event { Action action; unsigned image; unsigned class_id; unsigned category_id; std::uint64_t callback_pc; };
using Dispatch = std::function<Status(const Event &)>;
struct ImageSpec { unsigned id; std::vector<unsigned> dependencies; };
struct ClassSpec { unsigned id; unsigned image; unsigned superclass; std::uint64_t load_pc; std::uint64_t initialize_pc; };
struct CategorySpec { unsigned id; unsigned image; unsigned target_class; std::uint64_t load_pc; };
constexpr unsigned kNoClass = ~0u;
struct Lease { std::uint64_t ticket=0; unsigned class_id=kNoClass; std::uint32_t class_generation=0; std::uint32_t image_generation=0; };
struct Stats {
  std::uint64_t transitions=0, initialization_requests=0, callback_attempts=0;
  std::uint64_t callback_failures=0, successful_retirements=0;
  std::uint64_t deadlocks_detected=0, pins=0;
};
// This scheduler NEVER casts a guest PC to a host function pointer.
// The caller must validate native ObjC2 metadata, executable mappings and ABI.
class Engine final {
 public:
  explicit Engine(Dispatch dispatch, std::chrono::milliseconds timeout=std::chrono::seconds(2));
  Engine(const Engine &)=delete;
  Engine &operator=(const Engine &)=delete;
  Status register_image(const ImageSpec &spec);
  Status register_class(const ClassSpec &spec);
  Status register_category(const CategorySpec &spec);
  Status mark_macho_ready(unsigned image);
  Status load_image(unsigned image);
  Status first_message(unsigned class_id);
  Status begin_method(unsigned class_id, Lease &lease);
  Status end_method(const Lease &lease);
  Status retire_image(unsigned image);
  State class_initialization_state(unsigned class_id) const;
  Stats statistics() const;
 private:
  struct Image {
    ImageSpec spec;
    State state=State::uninitialized;
    bool macho_ready=false;
    std::uint32_t generation=1;
    std::uint64_t active=0;
    Status failure=Status::ok;
  };
  struct Class {
    ClassSpec spec;
    State load=State::uninitialized, initialize=State::uninitialized;
    std::thread::id initializing_thread;
    std::uint32_t generation=1;
    std::uint64_t active=0;
    Status failure=Status::ok;
  };
  struct Category { CategorySpec spec; State load=State::uninitialized; };
  struct Active { unsigned class_id; std::uint32_t class_gen, image_gen; };
  Status load_image_inner(unsigned image);
  Status load_class_inner(unsigned class_id);
  Status dispatch_event(const Event &event);
  Status initialize_class(unsigned class_id);
  Status fail_image(unsigned image, Status reason);
  bool would_deadlock(std::thread::id waiter, std::thread::id owner) const;
  bool image_has_dependents(unsigned id) const;
  void transition(State &state, State next);
  void set_failure(Class &cls, Status reason);
  Dispatch dispatch_;
  const std::chrono::milliseconds timeout_;
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::map<unsigned, Image> images_;
  std::map<unsigned, Class> classes_;
  std::map<unsigned, Category> categories_;
  std::map<std::thread::id,std::thread::id> waiting_;
  std::map<std::uint64_t, Active> active_;
  std::thread::id load_owner_;
  bool load_gate_=false;
  std::uint64_t next_ticket_=1;
  Stats stats_;
};
}
#endif
