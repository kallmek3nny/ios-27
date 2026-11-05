#include "ios27/phase25d_lifecycle.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
using namespace ios27::phase25d;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "failed %s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (false)

static void ordered_loads_and_initializers() {
  std::vector<std::string> trace;
  Engine e([&](const Event &v) {
    const auto action = v.action == Action::class_load ? "L" :
                        v.action == Action::category_load ? "C" : "I";
    trace.push_back(std::string(action) + std::to_string(v.action == Action::category_load ? v.category_id : v.class_id));
    return Status::ok;
  });
  for (unsigned i=0;i<6;++i) {
    CHECK(e.register_image({i,i?std::vector<unsigned>{i-1}:std::vector<unsigned>{}})==Status::ok);
    CHECK(e.register_class({i,i,i?i-1:kNoClass,0x1000u+i,0x2000u+i})==Status::ok);
    if (i) CHECK(e.register_category({i,i,i-1,0x3000u+i})==Status::ok);
    CHECK(e.mark_macho_ready(i)==Status::ok);
  }
  CHECK(e.first_message(5)==Status::ok);
  const std::vector<std::string> expected{"L0","L1","C1","L2","C2","L3","C3","L4","C4","L5","C5","I0","I1","I2","I3","I4","I5"};
  CHECK(trace==expected);
  CHECK(e.first_message(5)==Status::ok);
  CHECK(trace==expected);
  CHECK(e.retire_image(0)==Status::busy);
  for(unsigned i=6;i-->0;) CHECK(e.retire_image(i)==Status::ok);
  CHECK(e.first_message(5)==Status::retired);
  CHECK(e.statistics().pins==0);
}
static void concurrency_and_leases() {
  std::atomic<unsigned> calls{0}, failures{0};
  Engine e([&](const Event &v) {
    if(v.action==Action::class_initialize) { ++calls; std::this_thread::yield(); }
    return Status::ok;
  });
  CHECK(e.register_image({1,{}})==Status::ok);
  CHECK(e.register_class({1,1,kNoClass,1,2})==Status::ok);
  CHECK(e.mark_macho_ready(1)==Status::ok);
  std::vector<std::thread> threads;
  for(unsigned t=0;t<8;++t) threads.emplace_back([&]{
    for(unsigned i=0;i<15000;++i) {
      Lease l;
      if(e.begin_method(1,l)!=Status::ok) { ++failures; continue; }
      if(e.end_method(l)!=Status::ok) ++failures;
      if(e.end_method(l)!=Status::stale) ++failures;
    }
  });
  for(auto &thread:threads) thread.join();
  CHECK(failures==0);
  CHECK(calls==1);
  CHECK(e.statistics().initialization_requests==120000);
  CHECK(e.statistics().pins==0);
  CHECK(e.retire_image(1)==Status::ok);
}
static void reentrant_initialize_and_failure() {
  Engine *self=nullptr;
  unsigned calls=0;
  Engine e([&](const Event &v) {
    if(v.action==Action::class_initialize) { ++calls; CHECK(self->first_message(v.class_id)==Status::ok); }
    return Status::ok;
  });
  self=&e;
  CHECK(e.register_image({1,{}})==Status::ok);
  CHECK(e.register_class({1,1,kNoClass,1,2})==Status::ok);
  CHECK(e.mark_macho_ready(1)==Status::ok);
  CHECK(e.first_message(1)==Status::ok);
  CHECK(calls==1);
  Engine broken([](const Event &v) {
    return v.action==Action::class_initialize ? Status::callback_failed : Status::ok;
  });
  CHECK(broken.register_image({1,{}})==Status::ok);
  CHECK(broken.register_class({1,1,kNoClass,1,2})==Status::ok);
  CHECK(broken.mark_macho_ready(1)==Status::ok);
  CHECK(broken.first_message(1)==Status::callback_failed);
  CHECK(broken.first_message(1)==Status::callback_failed);
  CHECK(broken.class_initialization_state(1)==State::failed);
  CHECK(broken.statistics().callback_failures==1);
  CHECK(broken.retire_image(1)==Status::ok);
}
static void pinned_callbacks_and_teardown() {
  std::mutex gate;
  std::condition_variable cv;
  bool entered=false, proceed=false;
  Engine e([&](const Event &v) {
    if(v.action==Action::class_initialize) {
      std::unique_lock<std::mutex> lk(gate);
      entered=true; cv.notify_all();
      cv.wait(lk,[&]{return proceed;});
    }
    return Status::ok;
  });
  CHECK(e.register_image({1,{}})==Status::ok);
  CHECK(e.register_class({1,1,kNoClass,1,2})==Status::ok);
  CHECK(e.mark_macho_ready(1)==Status::ok);
  std::thread t([&]{ CHECK(e.first_message(1)==Status::ok); });
  {
    std::unique_lock<std::mutex> lk(gate);
    CHECK(cv.wait_for(lk,std::chrono::seconds(2),[&]{return entered;}));
  }
  CHECK(e.retire_image(1)==Status::busy);
  {
    std::lock_guard<std::mutex> lk(gate);
    proceed=true;
  }
  cv.notify_all();
  t.join();
  Lease l;
  CHECK(e.begin_method(1,l)==Status::ok);
  CHECK(e.retire_image(1)==Status::busy);
  CHECK(e.end_method(l)==Status::ok);
  CHECK(e.retire_image(1)==Status::ok);
  CHECK(e.begin_method(1,l)==Status::retired);
}
static void deadlock_detection() {
  Engine *self=nullptr;
  std::mutex gate;
  std::condition_variable cv;
  int entered=0;
  Engine e([&](const Event &v) {
    if(v.action!=Action::class_initialize) return Status::ok;
    {
      std::unique_lock<std::mutex> lk(gate);
      ++entered;
      cv.notify_all();
      if(!cv.wait_for(lk,std::chrono::seconds(2),[&]{return entered==2;})) return Status::timeout;
    }
    return self->first_message(v.class_id==1?2:1);
  });
  self=&e;
  CHECK(e.register_image({1,{}})==Status::ok);
  CHECK(e.register_class({1,1,kNoClass,1,2})==Status::ok);
  CHECK(e.register_class({2,1,kNoClass,3,4})==Status::ok);
  CHECK(e.mark_macho_ready(1)==Status::ok);
  Status a=Status::ok,b=Status::ok;
  std::thread t1([&]{a=e.first_message(1);});
  std::thread t2([&]{b=e.first_message(2);});
  t1.join();t2.join();
  CHECK(a==Status::deadlock || b==Status::deadlock);
  CHECK(e.statistics().deadlocks_detected>=1);
  CHECK(e.statistics().pins==0);
  CHECK(e.retire_image(1)==Status::ok);
}
static void generation_and_failed_load() {
  Engine e([](const Event &v)->Status {
    if(v.action==Action::class_load) throw 7;
    return Status::ok;
  });
  CHECK(e.register_image({1,{}})==Status::ok);
  CHECK(e.register_class({1,1,kNoClass,1,2})==Status::ok);
  CHECK(e.mark_macho_ready(1)==Status::ok);
  CHECK(e.load_image(1)==Status::callback_failed);
  CHECK(e.load_image(1)==Status::callback_failed);
  CHECK(e.retire_image(1)==Status::ok);
  CHECK(e.register_image({1,{}})==Status::ok);
  CHECK(e.register_class({1,1,kNoClass,0,0})==Status::ok);
  CHECK(e.mark_macho_ready(1)==Status::ok);
  CHECK(e.first_message(1)==Status::ok);
  Lease good;
  CHECK(e.begin_method(1,good)==Status::ok);
  Lease bad=good;
  ++bad.class_generation;
  CHECK(e.end_method(bad)==Status::stale);
  CHECK(e.end_method(good)==Status::ok);
  CHECK(e.retire_image(1)==Status::ok);
}
static void foreign_category_retention() {
  Engine e([](const Event &){return Status::ok;});
  CHECK(e.register_image({1,{}})==Status::ok);
  CHECK(e.register_class({1,1,kNoClass,1,2})==Status::ok);
  CHECK(e.register_image({2,{}})==Status::ok);
  CHECK(e.register_category({2,2,1,3})==Status::ok);
  CHECK(e.mark_macho_ready(1)==Status::ok);
  CHECK(e.mark_macho_ready(2)==Status::ok);
  CHECK(e.load_image(2)==Status::ok);
  CHECK(e.retire_image(1)==Status::busy);
  CHECK(e.retire_image(2)==Status::ok);
  CHECK(e.retire_image(1)==Status::ok);
}
static void stress_graphs() {
  std::uint64_t transitions=0, requests=0;
  for(unsigned cycle=0;cycle<5200;++cycle) {
    Engine e([](const Event &){return Status::ok;});
    for(unsigned id=0;id<4;++id) {
      CHECK(e.register_image({id,id?std::vector<unsigned>{id-1}:std::vector<unsigned>{}})==Status::ok);
      CHECK(e.register_class({id,id,id?id-1:kNoClass,1,2})==Status::ok);
      if(id) CHECK(e.register_category({id,id,id-1,3})==Status::ok);
      CHECK(e.mark_macho_ready(id)==Status::ok);
    }
    CHECK(e.first_message(3)==Status::ok);
    CHECK(e.first_message(3)==Status::ok);
    for(unsigned id=4;id-->0;) CHECK(e.retire_image(id)==Status::ok);
    Stats s=e.statistics();
    transitions+=s.transitions;
    requests+=s.initialization_requests;
    CHECK(s.pins==0);
  }
  CHECK(transitions>=100000);
  std::printf("phase25d: %llu modeled transitions, %llu stress requests; 120000 concurrent requests\n",
              static_cast<unsigned long long>(transitions),
              static_cast<unsigned long long>(requests));
}
int main() {
  ordered_loads_and_initializers();
  concurrency_and_leases();
  reentrant_initialize_and_failure();
  pinned_callbacks_and_teardown();
  deadlock_detection();
  generation_and_failed_load();
  foreign_category_retention();
  stress_graphs();
  std::puts("phase25d host scheduler PASS; verified Darwin callback execution: 0");
}
