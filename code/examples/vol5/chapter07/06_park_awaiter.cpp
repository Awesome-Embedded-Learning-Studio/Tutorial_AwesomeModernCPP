// park_awaiter.cpp —— Park：三件套的最小实现，挂起登记，等别人来恢复
#include <coroutine>
#include <cstdio>
#include <exception>
#include <vector>

struct Park {
  static std::vector<std::coroutine_handle<>> parked;
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<> h) { parked.push_back(h); }
  void await_resume() {}
};
std::vector<std::coroutine_handle<>> Park::parked;

// 最小任务类型，撑起 co_await（急启动，7.1 的同款）
struct Task {
  struct promise_type {
    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept { std::terminate(); }
  };

  std::coroutine_handle<> h;
  explicit Task(std::coroutine_handle<> hh) : h(hh) {}
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  ~Task() { if (h) h.destroy(); }
};

Task worker() {
  std::printf("  worker: 跑到 co_await Park{}，挂起登记\n");
  co_await Park{};
  std::printf("  worker: 被恢复，从挂起点接着跑\n");
}

int main() {
  Task t = worker();
  std::printf("main: 登记了 %zu 个句柄\n", Park::parked.size());
  Park::parked.back().resume();
  std::printf("main: worker 完工，程序结束\n");
  return 0;
}
