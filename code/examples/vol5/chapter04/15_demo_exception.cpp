// demo_exception.cpp —— set_exception：失败从工作线程走到 get 手里
#include <cstdio>
#include <exception>
#include <future>
#include <stdexcept>
#include <thread>

void divide(std::promise<int> out, int a, int b) {
  try {
    if (b == 0) {
      throw std::invalid_argument("除数为零，除法做不下去");
    }
    out.set_value(a / b);
  } catch (...) {
    out.set_exception(std::current_exception());   // 手里的异常原样装进共享状态
  }
}

void run(const char* tag, int a, int b) {
  std::promise<int> p;
  std::future<int> f = p.get_future();
  std::thread t(divide, std::move(p), a, b);
  try {
    std::printf("%s: %d / %d = %d\n", tag, a, b, f.get());
  } catch (const std::invalid_argument& e) {
    std::printf("%s: 捕获 invalid_argument，what() = %s\n", tag, e.what());
  }
  t.join();
}

int main() {
  run("坏除法", 6, 0);
  run("好除法", 6, 3);
  return 0;
}
