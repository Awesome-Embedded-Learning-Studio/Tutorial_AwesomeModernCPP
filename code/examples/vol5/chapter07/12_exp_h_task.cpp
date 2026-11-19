// exp_h_task.cpp —— 教学版 Task<int>：结果怎么回、异常怎么传、协程帧怎么收回
#include <coroutine>
#include <cstdio>
#include <exception>
#include <new>
#include <stdexcept>
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

// 以下为三条路径实测的实验协程与 main
Task<int> leaf(int x) {
  co_return x * 2;
}

Task<int> middle(int x) {
  int v = co_await leaf(x);
  co_return v + 1;
}

Task<int> pipeline() {
  int v = co_await middle(20);
  co_return v;
}

Task<int> boom(int x) {
  if (x == 13) throw std::runtime_error("叶子协程抛出的运行时错误");
  co_return x;
}

Task<int> relay(int x) {
  int v = co_await boom(x);   // boom 的异常在 co_await 这里重新浮出，relay 没接，存进自己的 promise
  co_return v;
}

Task<int> top(int x) {
  int v = co_await relay(x);  // relay 的异常在 co_await 这里重新浮出，top 也没接
  co_return v;
}

int main() {
  { // 路径一：结果沿链传回
    Task<int> t = pipeline();
    t.start();
    std::printf("路径一 结果: %d\n", t.result());
  }
  { // 路径二：异常逐级接力，在取结果的地方重新抛出
    Task<int> t = top(13);
    t.start();   // start 正常返回，异常不当场冒出来
    try {
      (void)t.result();
    } catch (const std::exception& e) {
      std::printf("路径二 异常: %s\n", e.what());
    }
  }
  { // 路径三：跑完的帧与没跑的帧，都由析构收回
    Task<int> run = pipeline();
    run.start();
    (void)run.result();
    Task<int> never = pipeline();   // 创建了，没 start，析构直接收帧
    std::printf("路径三 两个 Task 还在册的帧数: %d\n", g_frames);
  }
  std::printf("路径三 全部析构后在册帧数: %d\n", g_frames);
  return 0;
}
