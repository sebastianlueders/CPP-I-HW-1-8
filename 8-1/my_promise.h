#ifndef MY_PROMISE_H
#  define MY_PROMISE_H
#include<memory>
#include<thread>
#include<mutex>
#include<condition_variable>
#include<exception>
#include<stdexcept>
#include<atomic>
#include<variant>
using std::shared_ptr;
using std::unique_ptr;
using std::make_shared;
using std::make_unique;
using std::move;
using std::mutex;
using std::condition_variable;
using std::lock_guard;
using std::unique_lock;
using std::exception_ptr;
using std::rethrow_exception;
using std::runtime_error;

namespace mpcs {

// Accept any number of types T
template<class... Ts>
// Inherits from every single type T 
struct overloaded : Ts... 
{ 
  using Ts::operator()...; // Combines all operator overloads into this struct
};

// Allows CTAD so that we can use overloaded(lambda1, lambda2) without needing to write overloaded<T1, T2...>(lambda1, lambda2)
template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

template<class T> class MyPromise;

// lock free state & result
enum class StateInt : int { empty = 0, val = 1, exc =2 };

template<class T>
struct Result
{
  std::variant<T, std::exception_ptr> value; // variant to store value
  // Moves the result into internal storage; explicit to prevent implicit conversion from variant
  explicit Result(std::variant<T, std::exception_ptr> v) : value(std::move(v)) {} 
};

template<class T>
struct SharedState
{
  std::atomic<int> state{static_cast<int>(StateInt::empty)}; // used with wait/notify_one
  std::atomic<Result<T>*> result{nullptr}; // atomic pointer to the result
};

template<typename T>
class MyFuture {
public:
  MyFuture(MyFuture const &) = delete; // Injected class name
  MyFuture(MyFuture &&) = default;
  // MyFuture(MyFuture &&other) : sharedState{move(other.sharedState)} {}
  std::variant<T, std::exception_ptr> get() {
    // wait until state not empty
    int cur = static_cast<int>(StateInt::empty);
    while (sharedState->state.load(std::memory_order_acquire) == cur)
    {
      sharedState->state.wait(cur); // atomic wait
    }
    // atomically reads/loads the result pointer
    Result<T> * r = sharedState->result.load(std::memory_order_acquire);
    if (!r) throw runtime_error("No future result provided.");

    // move result out and free the heap object (single-consumer)
    auto out = std::move(r->value);
    delete r;
    sharedState->result.store(nullptr, std::memory_order_release);
    return out;
  }
private:
  friend class MyPromise<T>;
    MyFuture(shared_ptr<SharedState<T>> const &sharedState) 
      : sharedState(sharedState) {}
  shared_ptr<SharedState<T>> sharedState;
};

template<typename T>
class MyPromise
{
public:
  MyPromise() : sharedState{make_shared<SharedState<T>>()} {}

  void set_value(T const &value) {

    // Create a variant in the "T" state (index 0) and initialize it with value.
    auto r = new Result<T>(std::variant<T, std::exception_ptr>
    {
      std::in_place_index<0>, value
    });

    // atomically stores the result in sharedState
    sharedState->result.store(r, std::memory_order_release);

    // atomically updates the state to reflect a value has been delivered
    sharedState->state.store(static_cast<int>(StateInt::val), std::memory_order_release);

    // wakes a thread now that the result is ready
    sharedState->state.notify_one();
  }

  void set_exception(exception_ptr exc) {
    
    // Create new Result instance
    auto r = new Result<T>(std::variant<T, std::exception_ptr>
    {
      std::in_place_index<1>, exc
    });

    // store new exception 
    sharedState->result.store(r, std::memory_order_release);

    // Update state of the result to reflect an exception occurred
    sharedState->state.store(static_cast<int>(StateInt::exc), std::memory_order_release);

    sharedState->state.notify_one(); // wake one thread
  }

  MyFuture<T> get_future() {
    return sharedState;
  }
private:
  shared_ptr<SharedState<T>> sharedState; 
};
}
#endif

