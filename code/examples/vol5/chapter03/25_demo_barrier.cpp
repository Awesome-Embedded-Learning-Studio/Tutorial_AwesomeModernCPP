// demo_barrier.cpp —— 可重用的阶段同步：四条线程三轮齐步，完成函数汇轮数
#include <barrier>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <syncstream>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

int main() {
  constexpr int kWorkers = 4;
  int round = 0;
  std::barrier sync(kWorkers, [&round]() noexcept {
    ++round;
    std::osyncstream(std::cout)
        << "barrier  : 第 " << round << " 轮全员到齐，完成函数在线程 "
        << std::this_thread::get_id() << " 身上执行\n";
  });

  std::vector<std::thread> workers;
  for (int i = 0; i < kWorkers; ++i) {
    workers.emplace_back([&, i] {
      std::osyncstream(std::cout)
          << "worker " << i << " : 线程 " << std::this_thread::get_id()
          << " 进场\n";
      for (int p = 1; p <= 3; ++p) {
        std::this_thread::sleep_for(20ms * (p + i));   // 各人快慢不同
        sync.arrive_and_wait();
      }
    });
  }
  for (auto& t : workers) t.join();
  std::printf("main     : 三轮走完\n");
  return 0;
}
