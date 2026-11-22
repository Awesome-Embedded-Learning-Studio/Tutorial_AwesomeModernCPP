// exp_g_transform.cpp —— promise 配了 await_transform，co_await 一个 int 就有了落点
#include <coroutine>
#include <cstdio>
#include <exception>

struct SleepAwaiter {
  int ms;
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<> h) {
    std::printf("  [awaiter] 该等 %d 毫秒，真异步在这里交给定时器\n", ms);
    h.resume();   // 演示从简：登记完当场恢复
  }
  void await_resume() {}
};

struct Task {
  struct promise_type {
    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept { std::terminate(); }
    SleepAwaiter await_transform(int ms) { return SleepAwaiter{ms}; }
  };
  std::coroutine_handle<> h;
  explicit Task(std::coroutine_handle<promise_type> hh) : h(hh) {}
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  ~Task() { if (h) h.destroy(); }
};

Task countdown() {
  for (int i = 3; i > 0; --i) {
    std::printf("  倒计时 %d\n", i);
    co_await 250;
  }
  std::printf("  点火\n");
}

int main() {
  Task t = countdown();
  std::printf("main: 回到 main\n");
  return 0;
}
