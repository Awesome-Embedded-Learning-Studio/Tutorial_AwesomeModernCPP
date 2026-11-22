// frame_alloc.cpp —— 数一数协程状态的分配与释放，-O2 与 -O0 各跑一遍
#include <coroutine>
#include <cstdio>
#include <exception>
#include <new>

int g_allocs = 0;
int g_deallocs = 0;

template <typename T>
class Generator {
public:
  struct promise_type {
    T current_value;

    Generator get_return_object() {
      return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    std::suspend_always yield_value(T value) noexcept {
      current_value = value;
      return {};
    }
    void return_void() noexcept {}
    void unhandled_exception() { std::terminate(); }

    // promise 作用域里的分配函数，标准从这里找起
    void* operator new(std::size_t size) {
      ++g_allocs;
      return ::operator new(size);
    }
    void operator delete(void* ptr, std::size_t size) {
      ++g_deallocs;
      ::operator delete(ptr, size);
    }
  };

  explicit Generator(std::coroutine_handle<promise_type> h) : handle_(h) {}
  ~Generator() { if (handle_) handle_.destroy(); }
  Generator(const Generator&) = delete;
  Generator& operator=(const Generator&) = delete;

  bool next() {
    handle_.resume();
    return !handle_.done();
  }
  T value() const { return handle_.promise().current_value; }

private:
  std::coroutine_handle<promise_type> handle_;
};

Generator<int> seq(int limit) {
  for (int i = 1; i <= limit; ++i) co_yield i;
}

// 嵌套场景：三个生成器都活不到 run_nested 返回之后
void run_nested() {
  Generator<int> a = seq(2);
  Generator<int> b = seq(2);
  Generator<int> c = seq(2);
  while (a.next()) (void)a.value();
  while (b.next()) (void)b.value();
  while (c.next()) (void)c.value();
}

// 逃逸场景：生成器活着离开 make_escape，协程状态比调用者命长
Generator<int> make_escape() { return seq(2); }

int main() {
  run_nested();
  std::printf("嵌套场景之后：operator new %d 次，operator delete %d 次\n", g_allocs, g_deallocs);

  {
    Generator<int> g = make_escape();
    int taken = 0;
    while (g.next()) { ++taken; (void)g.value(); }
    std::printf("逃逸场景取到 %d 个值，随后：", taken);
  }
  std::printf("operator new %d 次，operator delete %d 次\n", g_allocs, g_deallocs);
  return 0;
}
