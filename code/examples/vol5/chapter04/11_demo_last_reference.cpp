// demo_last_reference.cpp —— 等待发生在最后一个引用放手时，不在每一次析构
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

double since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

void nap() { std::this_thread::sleep_for(300ms); }

int main() {
  std::future<void> f = std::async(std::launch::async, nap);
  std::shared_future<void> s1 = f.share();
  std::shared_future<void> s2 = s1;   // 同一状态现在挂在两个 shared_future 上

  auto t0 = Clock::now();
  { std::shared_future<void> s3 = s2; }
  std::printf("多出来的副本析构 %.3f ms\n", since(t0));

  t0 = Clock::now();
  s2 = std::shared_future<void>();    // 放掉一个引用，手里还剩 s1
  std::printf("放掉一个引用 %.3f ms\n", since(t0));

  t0 = Clock::now();
  s1 = std::shared_future<void>();    // 最后一个引用走人，析构开始等
  std::printf("最后一个引用 %.1f ms\n", since(t0));
  return 0;
}
