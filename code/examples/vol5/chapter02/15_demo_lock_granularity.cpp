// demo_lock_granularity.cpp —— 四把锁比一把锁快多少：四线程各加两百万次
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

using clock_type = std::chrono::steady_clock;

long long run(int lock_count) {
  const int kThreads = 4;
  const int kIters = 2000000;
  std::vector<std::mutex> locks(lock_count);
  std::vector<long long> counters(lock_count, 0);
  auto t0 = clock_type::now();
  std::vector<std::thread> threads;
  for (int i = 0; i < kThreads; ++i) {
    const int slot = (lock_count == 1) ? 0 : i;
    threads.emplace_back([&, slot] {
      for (int j = 0; j < kIters; ++j) {
        std::lock_guard<std::mutex> lock(locks[slot]);
        ++counters[slot];
      }
    });
  }
  for (auto& t : threads) t.join();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             clock_type::now() - t0).count();
}

int main() {
  for (int i = 0; i < 3; ++i) {
    std::printf("one lock for four counters: %lld ms\n", run(1));
    std::printf("one lock per counter:       %lld ms\n", run(4));
  }
  return 0;
}
