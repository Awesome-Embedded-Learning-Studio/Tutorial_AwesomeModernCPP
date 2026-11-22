// exp_i_loop.cpp —— 最小调度器：就绪队列加单线程循环，两段流水线排队跑
// Task 与 exp_h_task.cpp 相同，已拼在本文件顶部
#include <coroutine>
#include <cstdio>
#include <deque>
#include <exception>
#include <new>
#include <stdexcept>
#include <string>
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
      h.resume();   // 谁调 run，队列里的协程就在谁的线程上跑
    }
  }

private:
  std::deque<std::coroutine_handle<>> ready_;
};

// 让出：挂起，把自己放回队尾，轮到排在后面的协程
struct Yield {
  explicit Yield(Loop& loop) : loop_(loop) {}
  bool await_ready() noexcept { return false; }
  void await_suspend(std::coroutine_handle<> h) { loop_.schedule(h); }
  void await_resume() noexcept {}

private:
  Loop& loop_;
};

Task<std::string> fetch(Loop& loop, int id) {
  std::printf("  fetch(%d): 开始取数\n", id);
  co_await Yield{loop};
  std::printf("  fetch(%d): 取到了\n", id);
  co_return "数据" + std::to_string(id);
}

Task<std::string> process(Loop& loop, std::string data, int id) {
  std::printf("  process(%d): 拿到 %s，开始加工\n", id, data.c_str());
  co_await Yield{loop};
  std::printf("  process(%d): 加工完成\n", id);
  co_return data + "(已加工)";
}

Task<std::string> pipeline(Loop& loop, int id) {
  std::string data = co_await fetch(loop, id);
  std::string out = co_await process(loop, data, id);
  co_return out;
}

int main() {
  Loop loop;
  Task<std::string> a = pipeline(loop, 1);
  Task<std::string> b = pipeline(loop, 2);
  loop.schedule(a.handle());
  loop.schedule(b.handle());
  std::printf("== 循环开跑 ==\n");
  loop.run();
  std::printf("== 队列空了 ==\n");
  std::printf("a = %s\n", a.result().c_str());
  std::printf("b = %s\n", b.result().c_str());
  return 0;
}
