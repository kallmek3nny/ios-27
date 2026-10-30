#include "ios27/phase25d_lifecycle.hpp"
#include <utility>

namespace ios27::phase25d {
Engine::Engine(Dispatch dispatch, std::chrono::milliseconds timeout)
    : dispatch_(std::move(dispatch)), timeout_(timeout) {}

void Engine::transition(State &state, State next) {
  if (state != next) { state = next; ++stats_.transitions; }
}
Status Engine::dispatch_event(const Event &event) {
  if (!event.callback_pc) return Status::ok;
  if (!dispatch_) return Status::unsupported;
  try { return dispatch_(event); }
  catch (...) { return Status::callback_failed; }
}
Status Engine::register_image(const ImageSpec &spec) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (spec.dependencies.size() > 32 || spec.dependencies.size() > images_.size()) return Status::invalid;
  for (unsigned dep : spec.dependencies) {
    if (dep == spec.id || !images_.count(dep)) return Status::invalid;
  }
  auto it = images_.find(spec.id);
  if (it != images_.end() && it->second.state != State::retiring) return Status::duplicate;
  std::uint32_t generation = it == images_.end() ? 1 : it->second.generation;
  if (generation == 0) return Status::stale;
  Image fresh;
  fresh.spec = spec;
  fresh.generation = generation;
  images_[spec.id] = std::move(fresh);
  return Status::ok;
}
Status Engine::register_class(const ClassSpec &spec) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto image = images_.find(spec.image);
  if (image == images_.end() || image->second.macho_ready ||
      image->second.state != State::uninitialized) return Status::not_ready;
  if (spec.superclass == spec.id) return Status::cycle;
  if (spec.superclass != kNoClass && !classes_.count(spec.superclass)) return Status::invalid;
  auto it = classes_.find(spec.id);
  if (it != classes_.end() && it->second.initialize != State::retiring) return Status::duplicate;
  std::uint32_t generation = it == classes_.end() ? 1 : it->second.generation;
  if (generation == 0) return Status::stale;
  Class cls;
  cls.spec = spec;
  cls.generation = generation;
  classes_[spec.id] = std::move(cls);
  return Status::ok;
}
Status Engine::register_category(const CategorySpec &spec) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = images_.find(spec.image);
  if (it == images_.end() || it->second.macho_ready ||
      it->second.state != State::uninitialized) return Status::not_ready;
  if (!classes_.count(spec.target_class)) return Status::invalid;
  auto old = categories_.find(spec.id);
  if (old != categories_.end() && old->second.load != State::retiring) return Status::duplicate;
  categories_[spec.id] = Category{spec, State::uninitialized};
  return Status::ok;
}
Status Engine::mark_macho_ready(unsigned image) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = images_.find(image);
  if (it == images_.end() || it->second.state != State::uninitialized) return Status::invalid;
  it->second.macho_ready = true;
  return Status::ok;
}
// Serialize +load across threads, drop mutex during a guest callback, and
// impose a strict timeout on gate waiters. No unbounded polling.
Status Engine::load_image(unsigned image) {
  std::unique_lock<std::mutex> lock(mutex_);
  const auto deadline = std::chrono::steady_clock::now() + timeout_;
  if (load_gate_ && load_owner_ == std::this_thread::get_id()) {
    auto it = images_.find(image);
    return it != images_.end() && it->second.state == State::initialized
               ? Status::ok : Status::reentrant;
  }
  if (!changed_.wait_until(lock, deadline, [&] { return !load_gate_; })) return Status::timeout;
  load_gate_ = true;
  load_owner_ = std::this_thread::get_id();
  lock.unlock();
  const Status outcome = load_image_inner(image);
  lock.lock();
  load_gate_ = false;
  load_owner_ = {};
  lock.unlock();
  changed_.notify_all();
  return outcome;
}
Status Engine::fail_image(unsigned image, Status reason) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto &entry = images_.at(image);
  entry.failure = reason;
  transition(entry.state, State::failed);
  return reason;
}
Status Engine::load_image_inner(unsigned image) {
  ImageSpec spec;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = images_.find(image);
    if (it == images_.end()) return Status::invalid;
    if (it->second.state == State::initialized) return Status::ok;
    if (it->second.state == State::failed) return it->second.failure;
    if (it->second.state == State::retiring) return Status::retired;
    if (it->second.state == State::initializing) return Status::cycle;
    if (!it->second.macho_ready) return Status::not_ready;
    transition(it->second.state, State::initializing);
    spec = it->second.spec;
  }
  for (unsigned dependency : spec.dependencies) {
    const Status s = load_image_inner(dependency);
    if (s != Status::ok) return fail_image(image, s);
  }
  // Stable key order is the explicit deterministic scheduling contract.
  std::vector<unsigned> targets, categories;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &[id, cls] : classes_) if (cls.spec.image == image) targets.push_back(id);
    for (const auto &[id, cat] : categories_) if (cat.spec.image == image) categories.push_back(id);
  }
  for (unsigned id : targets) {
    const Status s = load_class_inner(id);
    if (s != Status::ok) return fail_image(image, s);
  }
  for (unsigned id : categories) {
    unsigned target;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      target = categories_.at(id).spec.target_class;
    }
    unsigned target_image;
    State target_state;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      target_image = classes_.at(target).spec.image;
      target_state = images_.at(target_image).state;
    }
    if (target_image != image && target_state == State::uninitialized) {
      Status dependency = load_image_inner(target_image);
      if (dependency != Status::ok) return fail_image(image, dependency);
    }
    Status s = load_class_inner(target);
    if (s != Status::ok) return fail_image(image, s);
    Event event{};
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto &category = categories_.at(id);
      if (category.load == State::initialized) continue;
      event = Event{Action::category_load, image, target, id, category.spec.load_pc};
      transition(category.load, State::initializing);
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      ++images_.at(image).active;
      ++images_.at(classes_.at(target).spec.image).active;
      ++classes_.at(target).active;
      stats_.pins += 3;
      if (event.callback_pc) ++stats_.callback_attempts;
    }
    s = dispatch_event(event);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      --images_.at(image).active;
      --images_.at(classes_.at(target).spec.image).active;
      --classes_.at(target).active;
      stats_.pins -= 3;
      transition(categories_.at(id).load, s == Status::ok ? State::initialized : State::failed);
      if (s != Status::ok) ++stats_.callback_failures;
    }
    changed_.notify_all();
    if (s != Status::ok) return fail_image(image, s);
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    transition(images_.at(image).state, State::initialized);
  }
  changed_.notify_all();
  return Status::ok;
}
Status Engine::load_class_inner(unsigned class_id) {
  ClassSpec spec;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = classes_.find(class_id);
    if (it == classes_.end()) return Status::invalid;
    if (it->second.load == State::initialized) return Status::ok;
    if (it->second.load == State::failed) return it->second.failure;
    if (it->second.load == State::initializing) return Status::cycle;
    if (it->second.load == State::retiring) return Status::retired;
    transition(it->second.load, State::initializing);
    spec = it->second.spec;
  }
  if (spec.superclass != kNoClass) {
    unsigned super_image = 0;
    State super_image_state = State::uninitialized;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto &super = classes_.at(spec.superclass);
      super_image = super.spec.image;
      super_image_state = images_.at(super_image).state;
    }
    if (super_image != spec.image && super_image_state == State::uninitialized) {
      Status dependency = load_image_inner(super_image);
      if (dependency != Status::ok) {
        std::lock_guard<std::mutex> lock(mutex_);
        set_failure(classes_.at(class_id), dependency);
        return dependency;
      }
    }
    Status s = load_class_inner(spec.superclass);
    if (s != Status::ok) {
      std::lock_guard<std::mutex> lock(mutex_);
      set_failure(classes_.at(class_id), s);
      return s;
    }
  }
  Event event{Action::class_load, spec.image, spec.id, kNoClass, spec.load_pc};
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++images_.at(spec.image).active;
    ++classes_.at(spec.id).active;
    stats_.pins += 2;
    if (event.callback_pc) ++stats_.callback_attempts;
  }
  Status s = dispatch_event(event);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    --images_.at(spec.image).active;
    --classes_.at(spec.id).active;
    stats_.pins -= 2;
    if (s == Status::ok) transition(classes_.at(class_id).load, State::initialized);
    else { ++stats_.callback_failures; set_failure(classes_.at(class_id), s); }
  }
  changed_.notify_all();
  return s;
}
void Engine::set_failure(Class &cls, Status reason) {
  cls.failure = reason;
  transition(cls.initialize, State::failed);
  transition(cls.load, State::failed);
}
bool Engine::would_deadlock(std::thread::id waiter, std::thread::id owner) const {
  for (std::size_t i = 0; i <= waiting_.size(); ++i) {
    if (owner == waiter) return true;
    const auto it = waiting_.find(owner);
    if (it == waiting_.end()) return false;
    owner = it->second;
  }
  return true;
}
Status Engine::first_message(unsigned class_id) {
  unsigned image = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.initialization_requests;
    auto it = classes_.find(class_id);
    if (it == classes_.end()) return Status::invalid;
    image = it->second.spec.image;
  }
  Status s = load_image(image);
  return s == Status::ok ? initialize_class(class_id) : s;
}
Status Engine::initialize_class(unsigned class_id) {
  unsigned superclass = kNoClass;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = classes_.find(class_id);
    if (it == classes_.end()) return Status::invalid;
    superclass = it->second.spec.superclass;
  }
  if (superclass != kNoClass) {
    Status s = initialize_class(superclass);
    if (s != Status::ok) return s;
  }
  Event event{};
  {
    std::unique_lock<std::mutex> lock(mutex_);
    auto &cls = classes_.at(class_id);
    const auto deadline = std::chrono::steady_clock::now() + timeout_;
    while (cls.initialize == State::initializing) {
      if (cls.initializing_thread == std::this_thread::get_id()) return Status::ok;
      const auto waiter = std::this_thread::get_id();
      if (would_deadlock(waiter, cls.initializing_thread)) {
        ++stats_.deadlocks_detected;
        return Status::deadlock;
      }
      waiting_[waiter] = cls.initializing_thread;
      const bool complete = changed_.wait_until(lock, deadline, [&cls] {
        return cls.initialize != State::initializing;
      });
      waiting_.erase(waiter);
      if (!complete) return Status::timeout;
    }
    if (cls.initialize == State::initialized) return Status::ok;
    if (cls.initialize == State::failed) return cls.failure;
    if (cls.initialize == State::retiring) return Status::retired;
    if (cls.load != State::initialized ||
        images_.at(cls.spec.image).state != State::initialized) return Status::not_ready;
    transition(cls.initialize, State::initializing);
    cls.initializing_thread = std::this_thread::get_id();
    ++cls.active;
    ++images_.at(cls.spec.image).active;
    stats_.pins += 2;
    event = Event{Action::class_initialize, cls.spec.image, class_id, kNoClass, cls.spec.initialize_pc};
    if (event.callback_pc) ++stats_.callback_attempts;
  }
  Status s = dispatch_event(event);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    Class &cls = classes_.at(class_id);
    cls.initializing_thread = {};
    --cls.active;
    --images_.at(cls.spec.image).active;
    stats_.pins -= 2;
    if (s == Status::ok) transition(cls.initialize, State::initialized);
    else {
      cls.failure = s;
      ++stats_.callback_failures;
      transition(cls.initialize, State::failed);
    }
  }
  changed_.notify_all();
  return s;
}
Status Engine::begin_method(unsigned class_id, Lease &lease) {
  Status s = first_message(class_id);
  if (s != Status::ok) return s;
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = classes_.find(class_id);
  if (it == classes_.end() || it->second.initialize != State::initialized) return Status::retired;
  const unsigned image = it->second.spec.image;
  if (images_.at(image).state != State::initialized || next_ticket_ == 0) return Status::retired;
  const auto ticket = next_ticket_++;
  active_[ticket] = Active{class_id, it->second.generation, images_.at(image).generation};
  ++it->second.active;
  ++images_.at(image).active;
  stats_.pins += 2;
  lease = Lease{ticket, class_id, it->second.generation, images_.at(image).generation};
  return Status::ok;
}
Status Engine::end_method(const Lease &lease) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = active_.find(lease.ticket);
  if (it == active_.end() || it->second.class_id != lease.class_id ||
      it->second.class_gen != lease.class_generation ||
      it->second.image_gen != lease.image_generation) return Status::stale;
  auto &cls = classes_.at(lease.class_id);
  --cls.active;
  --images_.at(cls.spec.image).active;
  stats_.pins -= 2;
  active_.erase(it);
  changed_.notify_all();
  return Status::ok;
}
bool Engine::image_has_dependents(unsigned image) const {
  for (const auto &[id, candidate] : images_) {
    if (id == image || candidate.state == State::retiring) continue;
    for (unsigned dependency : candidate.spec.dependencies)
      if (dependency == image) return true;
  }
  for (const auto &[id, cls] : classes_) {
    (void)id;
    if (cls.initialize == State::retiring || cls.spec.superclass == kNoClass) continue;
    auto sup = classes_.find(cls.spec.superclass);
    if (sup != classes_.end() && sup->second.spec.image == image &&
        cls.spec.image != image) return true;
  }
  for (const auto &[id, category] : categories_) {
    (void)id;
    if (category.load == State::retiring || category.spec.image == image) continue;
    auto target = classes_.find(category.spec.target_class);
    if (target != classes_.end() && target->second.spec.image == image) return true;
  }
  return false;
}
Status Engine::retire_image(unsigned image) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = images_.find(image);
  if (it == images_.end()) return Status::invalid;
  if (it->second.state == State::retiring) return Status::retired;
  if (it->second.state == State::initializing || it->second.active ||
      image_has_dependents(image)) return Status::busy;
  for (const auto &[id, cls] : classes_) {
    (void)id;
    if (cls.spec.image == image &&
        (cls.active || cls.initialize == State::initializing)) return Status::busy;
  }
  transition(it->second.state, State::retiring);
  if (++it->second.generation == 0) return Status::stale;
  for (auto &[id, cls] : classes_) {
    (void)id;
    if (cls.spec.image != image) continue;
    transition(cls.initialize, State::retiring);
    transition(cls.load, State::retiring);
    if (++cls.generation == 0) return Status::stale;
  }
  for (auto &[id, cat] : categories_) {
    (void)id;
    if (cat.spec.image == image) transition(cat.load, State::retiring);
  }
  ++stats_.successful_retirements;
  changed_.notify_all();
  return Status::ok;
}
State Engine::class_initialization_state(unsigned class_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = classes_.find(class_id);
  return it == classes_.end() ? State::retiring : it->second.initialize;
}
Stats Engine::statistics() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return stats_;
}
}
