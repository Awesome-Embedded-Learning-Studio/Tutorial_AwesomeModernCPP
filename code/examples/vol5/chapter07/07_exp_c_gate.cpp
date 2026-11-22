// exp_c_gate.cpp —— 最小可等待对象：一扇带值的门
#include <coroutine>
#include <cstdio>
#include <exception>
#include <optional>

// 门：还没有值时，co_await 它的协程挂起登记，有值了，恢复时把值带回来
struct Gate {
  std::optional<int> value;            // 门里的值
  std::coroutine_handle<> waiter{};    // 登记在门口的协程，至多一位

  bool await_ready() { return value.has_value(); }  // 值已经在，不必挂起
  void await_suspend(std::coroutine_handle<> h) { waiter = h; }
  int await_resume() { return *value; }             // 恢复时把门里的值带回去

  void set(int v) {  // 送值进门，顺路叫醒登记的人
    value = v;
    if (waiter) {
      auto w = waiter;
      waiter = {};
      w.resume();
    }
  }
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
  };

  std::coroutine_handle<> h;
  explicit Task(std::coroutine_handle<promise_type> hh) : h(hh) {}
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  ~Task() { if (h) h.destroy(); }
};

Task consumer(Gate& gate) {
  std::printf("  consumer: 来到门口，准备等一个值\n");
  int v = co_await gate;
  std::printf("  consumer: 恢复了，从 await_resume 拿到 %d\n", v);
}

int main() {
  { // 幕一：值后到，协程挂起登记，set 把它叫醒
    Gate gate;
    Task t = consumer(gate);
    std::printf("main: consumer 登记了吗？waiter=%s\n", gate.waiter ? "有" : "没有");
    std::printf("main: 送 42 进门\n");
    gate.set(42);
    std::printf("main: 回到 main，一幕结束\n");
  }
  { // 幕二：值提前在门里，await_ready 走快路径，协程根本不挂起
    Gate gate;
    gate.set(7);
    std::printf("main: 幕二开场，7 已经在门里\n");
    Task t = consumer(gate);
    std::printf("main: 幕二结束，waiter=%s\n", gate.waiter ? "有" : "没有");
  }
  return 0;
}
