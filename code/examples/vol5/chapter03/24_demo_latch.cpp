// demo_latch.cpp —— 一次性倒计时：三件初始化活，一个等待方
#include <chrono>
#include <cstdio>
#include <latch>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
  std::latch ready(3);
  const char* names[3] = {"logger", "config", "network"};
  std::vector<std::thread> inits;
  for (int i = 0; i < 3; ++i) {
    inits.emplace_back([&ready, i, &names] {
      std::this_thread::sleep_for(50ms * (3 - i));   // 各自耗时 150/100/50 ms
      std::printf("[%s] 就绪\n", names[i]);
      std::fflush(stdout);
      ready.count_down();
    });
  }
  auto t0 = Clock::now();
  ready.wait();
  std::printf("main   : %5.1f ms 全部就绪，业务开始\n", ms_since(t0));
  for (auto& t : inits) t.join();

  std::latch fired(1);
  fired.count_down();   // 递减发生在等待之前
  auto t1 = Clock::now();
  fired.wait();         // 计数已经是 0，当场返回
  std::printf("main   : count_down 走在 wait 前面，wait 用时 %.3f ms\n",
              ms_since(t1));
  return 0;
}
