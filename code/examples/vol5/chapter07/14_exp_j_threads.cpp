// exp_j_threads.cpp —— 谁在哪个线程上恢复：谁调 resume，协程体就在谁的线程上接着跑
// Task 与 exp_h_task.cpp 相同（handle() 访问器用上了），Loop 与 exp_i_loop.cpp 相同，均已拼进本文件
#include <coroutine>
#include <cstdio>
#include <deque>
#include <exception>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

int g_frames = 0;   // 协程帧的在册计数，教学演示销毁时机用，非线程安全

template <typename T>
class Task {
public:
  struct promise_type {
    T result{};
    std::exception_ptr failure{};
    std::coroutine_handle<> continuation{};   // 等我做完的上一级协程，根 Task 手里没有

    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept { return {}; }   // 懒启动：创建时不跑
    void return_value(T value) { result = std::move(value); }
    void unhandled_exception() { failure = std::current_exception(); }

    // 最终挂起点的交接，sym 姿势：有人等就交回去，没人是根，交还最初的恢复者
    auto final_suspend() noexcept {
      struct Done {
        bool await_ready() noexcept { return false; }
        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<promise_type> me) noexcept {
          auto c = me.promise().continuation;
          if (!c) return std::noop_coroutine();
          return c;
        }
        void await_resume() noexcept {}
      };
      return Done{};
    }

    // 帧计数，7.2 frame_alloc 的同款手法
    void* operator new(std::size_t size) {
      ++g_frames;
      return ::operator new(size);
    }
    void operator delete(void* ptr, std::size_t size) {
      --g_frames;
      ::operator delete(ptr, size);
    }
  };

  explicit Task(std::coroutine_handle<promise_type> h) : h_(h) {}
  ~Task() { if (h_) h_.destroy(); }   // RAII：帧的收回交给析构函数
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  Task(Task&& o) noexcept : h_(o.h_) { o.h_ = {}; }
  Task& operator=(Task&& o) noexcept {
    if (this != &o) {
      if (h_) h_.destroy();
      h_ = o.h_;
      o.h_ = {};
    }
    return *this;
  }

  // 三件套：Task 自己就是 awaiter，co_await 一个 Task 就是在等它做完
  bool await_ready() noexcept { return false; }
  std::coroutine_handle<> await_suspend(std::coroutine_handle<> waiter) noexcept {
    h_.promise().continuation = waiter;   // 登记在前
    return h_;                            // 启动在后，sym 姿势把子协程跑起来
  }
  T await_resume() { return take_result(); }

  void start() { h_.resume(); }   // 根 Task 的启动口：谁调它，协程体就在谁的线程上跑
  T result() { return take_result(); }
  std::coroutine_handle<promise_type> handle() const { return h_; }   // 交给调度器用

private:
  T take_result() {
    if (h_.promise().failure)
      std::rethrow_exception(h_.promise().failure);
    return std::move(h_.promise().result);
  }

  std::coroutine_handle<promise_type> h_;
};

class Loop {
public:
  void schedule(std::coroutine_handle<> h) { ready_.push_back(h); }
  void run() {
    while (!ready_.empty()) {
      auto h = ready_.front();
      ready_.pop_front();
      h.resume();
    }
  }

private:
  std::deque<std::coroutine_handle<>> ready_;
};

static std::string tid() {
  std::ostringstream oss;
  oss << std::this_thread::get_id();
  return oss.str();
}

Task<int> leaf(int x) {
  std::printf("  leaf(%d)       在线程 %s\n", x, tid().c_str());
  co_return x + 1;
}

Task<int> root(int x) {
  std::printf("  root 前半      在线程 %s\n", tid().c_str());
  int v = co_await leaf(x);
  std::printf("  root 后半 v=%d 在线程 %s\n", v, tid().c_str());
  co_return v;
}

int main() {
  std::printf("main 在线程 %s\n", tid().c_str());
  { // 幕一：main 自己启动
    Task<int> t = root(10);
    t.start();
    std::printf("幕一 result=%d\n", t.result());
  }
  { // 幕二：start 交给 driver 线程
    Task<int> t = root(20);
    std::thread driver([&t] { t.start(); });
    driver.join();
    std::printf("幕二 result=%d\n", t.result());
  }
  { // 幕三：任务在 main 创建，恢复发生在跑循环的 driver 线程上
    Loop loop;
    Task<int> t = root(30);
    loop.schedule(t.handle());
    std::thread driver([&loop] { loop.run(); });
    driver.join();
    std::printf("幕三 result=%d\n", t.result());
  }
  return 0;
}
