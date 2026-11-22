// demo_get.cpp —— 发起、自己干一小段活、get 把结果等回来
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int square(int x) {
  std::this_thread::sleep_for(300ms);   // 顶替一段真实的计算
  return x * x;
}

int main() {
  auto t0 = Clock::now();
  std::future<int> result = std::async(square, 21);
  std::printf("main  : %5.1f ms 计算发起，future 在手\n", ms_since(t0));

  for (int i = 0; i < 5; ++i) {         // main 自己的一小段活
    std::this_thread::sleep_for(10ms);
  }
  std::printf("main  : %5.1f ms 自己的活干完，来取结果\n", ms_since(t0));

  int value = result.get();             // 结果没就绪就等，就绪才返回
  std::printf("main  : %5.1f ms get 返回，square(21) = %d\n", ms_since(t0), value);
  std::printf("main  : get 之后 valid() = %d\n", result.valid() ? 1 : 0);
  return 0;
}
