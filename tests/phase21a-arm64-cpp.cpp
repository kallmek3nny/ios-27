/* Real freestanding Clang++ 17 arm64-apple-darwin class graph, no Apple SDK.
 * Member destructors, virtual calls and recursive templates generate genuine
 * imported pthread call relocations in a real Mach-O MH_OBJECT. */
#include "ios27/libsystem21a_abi.h"

extern "C" {
int pthread_mutex_init(ios27_21a_mutex *, const ios27_21a_mutexattr *);
int pthread_mutex_lock(ios27_21a_mutex *);
int pthread_mutex_unlock(ios27_21a_mutex *);
int pthread_mutex_destroy(ios27_21a_mutex *);
int pthread_cond_init(ios27_21a_cond *, const ios27_21a_condattr *);
int pthread_cond_signal(ios27_21a_cond *);
int pthread_cond_destroy(ios27_21a_cond *);
int pthread_rwlock_init(ios27_21a_rwlock *, const ios27_21a_rwlockattr *);
int pthread_rwlock_rdlock(ios27_21a_rwlock *);
int pthread_rwlock_unlock(ios27_21a_rwlock *);
int pthread_rwlock_destroy(ios27_21a_rwlock *);
int pthread_once(ios27_21a_once *, void (*)());
}
namespace phase21a {
static ios27_21a_mutex mutex;
static ios27_21a_cond condition;
static ios27_21a_rwlock rwlock;
static ios27_21a_once gate;
static volatile unsigned long shared_counter;
static void initialize() { shared_counter = 7u; }

class Guard final {
    ios27_21a_mutex *native_;
public:
    explicit Guard(ios27_21a_mutex *lock) : native_(lock) {
        (void)pthread_mutex_lock(native_);
    }
    ~Guard() { (void)pthread_mutex_unlock(native_); }
    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;
};
class Operation {
public:
    virtual unsigned step(unsigned value) = 0;
    virtual ~Operation() = default;
};
template <unsigned Level>
struct Recursive {
    static unsigned apply(unsigned input) {
        return Recursive<Level - 1u>::apply(input + Level);
    }
};
template <>
struct Recursive<0u> {
    static unsigned apply(unsigned input) { return input; }
};
class Counter final : public Operation {
    unsigned offset_;
public:
    explicit Counter(unsigned offset) : offset_(offset) {}
    unsigned step(unsigned input) override {
        (void)pthread_once(&gate, initialize);
        Guard hold(&mutex);
        if (pthread_rwlock_rdlock(&rwlock) == 0) {
            shared_counter += Recursive<4u>::apply(input + offset_);
            (void)pthread_cond_signal(&condition);
            (void)pthread_rwlock_unlock(&rwlock);
        }
        return static_cast<unsigned>(shared_counter);
    }
};
static unsigned dynamic_dispatch(Operation &operation, unsigned n) {
    unsigned result = 0;
    for (unsigned i = 0; i < n; ++i) result ^= operation.step(i);
    return result;
}
}  // namespace phase21a
extern "C" __attribute__((used)) unsigned phase21a_real_cpp_call_graph(unsigned n) {
    using namespace phase21a;
    if (pthread_mutex_init(&mutex, nullptr) != 0) return 1u;
    if (pthread_cond_init(&condition, nullptr) != 0) return 2u;
    if (pthread_rwlock_init(&rwlock, nullptr) != 0) return 3u;
    Counter counter(n & 7u);
    unsigned value = dynamic_dispatch(counter, n);
    (void)pthread_rwlock_destroy(&rwlock);
    (void)pthread_cond_destroy(&condition);
    (void)pthread_mutex_destroy(&mutex);
    return value;
}
