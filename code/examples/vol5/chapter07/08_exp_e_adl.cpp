// exp_e_adl.cpp —— 给改不了的第三方类型配 operator co_await
#include <coroutine>
#include <cstdio>
#include <exception>

namespace vendor {  // 假装这是别人的库，源码动不了
  struct Ticket {
    int number;
    explicit Ticket(int n) : number(n) {}
  };

  struct TicketAwaiter {
    Ticket t;
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<> h) {
      std::printf("  [awaiter] 协程挂好了，真异步在这里交给调度器\n");
      h.resume();   // 演示从简：登记完当场恢复
    }
    int await_resume() { return t.number; }
  };

  // 定制点住进 vendor 命名空间，ADL 顺着 Ticket 的来路就找到它
  TicketAwaiter operator co_await(Ticket t) { return TicketAwaiter{t}; }
}

struct Task {
  struct promise_type {
    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept { std::terminate(); }
  };
  std::coroutine_handle<> h;
  explicit Task(std::coroutine_handle<promise_type> hh) : h(hh) {}
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  ~Task() { if (h) h.destroy(); }
};

Task caller() {
  std::printf("  caller: 写下 co_await vendor::Ticket(7)\n");
  int n = co_await vendor::Ticket(7);
  std::printf("  caller: await_resume 把排队号 %d 带了回来\n", n);
}

int main() {
  Task t = caller();
  std::printf("main: 回到 main\n");
  return 0;
}
