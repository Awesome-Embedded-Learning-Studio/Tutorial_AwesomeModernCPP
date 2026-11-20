// gen_min.cpp —— 能跑通的最小生成器，co_yield 产出平方数
#include <coroutine>
#include <cstdio>
#include <exception>

template <typename T>
class Generator {
public:
  struct promise_type {
    T current_value;
    std::exception_ptr failure;

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
    void unhandled_exception() { failure = std::current_exception(); }
  };

  explicit Generator(std::coroutine_handle<promise_type> handle) : handle_(handle) {}
  ~Generator() {
    if (handle_) handle_.destroy();
  }
  Generator(const Generator&) = delete;
  Generator& operator=(const Generator&) = delete;

  bool next() {
    handle_.resume();
    if (handle_.promise().failure) {
      std::rethrow_exception(handle_.promise().failure);
    }
    return !handle_.done();
  }

  T value() const { return handle_.promise().current_value; }

private:
  std::coroutine_handle<promise_type> handle_;
};

Generator<int> squares(int limit) {
  for (int i = 1; i <= limit; ++i) {
    co_yield i * i;
  }
}

int main() {
  std::printf("创建 squares(5)，此刻函数体一行没跑\n");
  Generator<int> gen = squares(5);
  while (gen.next()) {
    std::printf("拿到 %d\n", gen.value());
  }
  std::printf("生成器耗尽，destroy 交给析构函数\n");
  return 0;
}
