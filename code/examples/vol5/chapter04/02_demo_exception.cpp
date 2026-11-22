// demo_exception.cpp —— 任务里抛的异常，get 在调用线程重抛
#include <cstdio>
#include <future>
#include <stdexcept>

int divide(int a, int b) {
  if (b == 0) {
    throw std::invalid_argument("除数为零，除法做不下去");
  }
  return a / b;
}

int main() {
  std::future<int> bad = std::async(divide, 6, 0);
  try {
    int value = bad.get();              // 这一行抛出任务里存下的那个异常
    std::printf("main: 拿到 %d\n", value);
  } catch (const std::invalid_argument& e) {
    std::printf("main: 捕获 invalid_argument，what() = %s\n", e.what());
  }

  std::future<int> good = std::async(divide, 6, 3);
  std::printf("main: divide(6, 3) = %d\n", good.get());
  return 0;
}
