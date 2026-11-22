// demo_semaphore.cpp —— 计数信号量：8 个任务抢 3 个名额，外加一次超时
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <semaphore>
#include <syncstream>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

int main() {
  std::counting_semaphore<> slots(3);   // 3 个名额
  std::atomic<int> inside{0};
  std::atomic<int> peak{0};

  std::vector<std::thread> tasks;
  for (int i = 0; i < 8; ++i) {
    tasks.emplace_back([&, i] {
      slots.acquire();                  // 没名额就睡，等到 release 才回
      int now = ++inside;
      int old = peak.load();
      while (now > old && !peak.compare_exchange_weak(old, now)) {}
      std::this_thread::sleep_for(100ms);
      --inside;
      slots.release();
      std::osyncstream(std::cout)
          << "task " << i << " : 活干完，名额还回去\n";
    });
  }
  for (auto& t : tasks) t.join();
  std::printf("main: 8 个任务全做完，同时在场的峰值 %d（上限 3）\n", peak.load());

  std::counting_semaphore<> none(0);
  auto t0 = Clock::now();
  bool got = none.try_acquire_for(50ms);
  double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
  std::printf("main: 计数 0 上等名额，try_acquire_for 返回 %d，耗时 %.1f ms\n",
              got ? 1 : 0, ms);
  return 0;
}
