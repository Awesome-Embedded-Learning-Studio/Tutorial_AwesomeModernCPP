// lazy_start.cpp —— 懒启动与终点站：协程体什么时候真的跑，帧什么时候收回
#include <coroutine>
#include <cstdio>
#include <exception>

struct Trace {
  struct promise_type {
    Trace get_return_object() {
      return Trace{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept {
      std::printf("  [协程] initial_suspend 挂起，控制权交回调用者\n");
      return {};
    }
    std::suspend_always final_suspend() noexcept {
      std::printf("  [协程] final_suspend 挂起，等 destroy\n");
      return {};
    }
    void return_void() noexcept {}
    void unhandled_exception() { std::terminate(); }
  };

  explicit Trace(std::coroutine_handle<promise_type> h) : handle_(h) {}
  ~Trace() {
    if (handle_) {
      std::printf("  [main ] Trace 析构，调 destroy 收回协程帧\n");
      handle_.destroy();
    }
  }
  Trace(const Trace&) = delete;
  Trace& operator=(const Trace&) = delete;

  void resume() { handle_.resume(); }
  bool done() const { return handle_.done(); }

private:
  std::coroutine_handle<promise_type> handle_;
};

Trace demo() {
  std::printf("  [协程] 函数体第一行\n");
  std::printf("  [协程] 函数体第二行\n");
  co_return;
}

int main() {
  std::printf("[main ] 调用 demo()\n");
  {
    Trace t = demo();
    std::printf("[main ] 拿到返回对象，函数体一行没跑，done=%d\n", t.done() ? 1 : 0);
    std::printf("[main ] 第一次 resume\n");
    t.resume();
    std::printf("[main ] 回到 main，done=%d\n", t.done() ? 1 : 0);
  }
  std::printf("[main ] Trace 析构完，程序结束\n");
  return 0;
}
