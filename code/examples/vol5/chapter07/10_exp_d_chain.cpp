// exp_d_chain.cpp —— 深链协程：两种控制权交接姿势，量栈的读数
// 用法：./exp_d_chain <naive|sym> <链深>
#include <coroutine>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

static bool g_sym = false;   // sym：await_suspend 返回句柄；naive：当场 resume

struct ChainTask {
  struct promise_type {
    std::coroutine_handle<> continuation{};   // 等我做完的上一级协程，根协程没有

    ChainTask get_return_object() {
      return ChainTask{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept { std::terminate(); }

    // 完成时的交接：有人等就把控制权交回去，没人是根，就交还给最初的恢复者
    auto final_suspend() noexcept {
      struct Done {
        bool await_ready() noexcept { return false; }
        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<promise_type> me) noexcept {
          auto c = me.promise().continuation;
          if (!c) return std::noop_coroutine();
          if (g_sym) return c;
          c.resume();                // naive：上一级在栈的更深处接着跑
          return std::noop_coroutine();
        }
        void await_resume() noexcept {}
      };
      return Done{};
    }
  };

  std::coroutine_handle<promise_type> h;
  explicit ChainTask(std::coroutine_handle<promise_type> hh) : h(hh) {}
  ChainTask(const ChainTask&) = delete;
  ChainTask& operator=(const ChainTask&) = delete;
  ~ChainTask() { if (h) h.destroy(); }

  bool await_ready() noexcept { return false; }
  std::coroutine_handle<> await_suspend(std::coroutine_handle<> waiter) noexcept {
    h.promise().continuation = waiter;
    if (g_sym) return h;   // sym：挂起自己，把句柄交出去，让编译器接手
    h.resume();            // naive：当场把子协程跑完，才回到这里
    return std::noop_coroutine();
  }
  void await_resume() noexcept {}
};

ChainTask chain(int depth) {
  if (depth % 100000 == 0) {
    std::printf("    到达深度 %d\n", depth);
    std::fflush(stdout);
  }
  if (depth > 0) {
    co_await chain(depth - 1);
  }
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("用法: %s <naive|sym> <链深>\n", argv[0]);
    return 1;
  }
  g_sym = (std::strcmp(argv[1], "sym") == 0);
  int depth = std::atoi(argv[2]);
  std::printf("== %s 模式，链深 %d ==\n", g_sym ? "sym" : "naive", depth);
  ChainTask root = chain(depth);
  root.h.resume();
  std::printf("== 根协程完工，控制权回到 main ==\n");
  return 0;
}
