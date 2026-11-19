// demo_starvation.cpp —— 锁不排队：八条线程抢 200 毫秒，观察者排不上
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

std::atomic_flag busy = ATOMIC_FLAG_INIT;

void lock() { while (busy.test_and_set(std::memory_order_acquire)) {} }
void unlock() { busy.clear(std::memory_order_release); }

int main() {
  std::atomic<long> hog_turns{0};
  std::atomic<long> victim_turns{0};
  std::atomic<bool> stop{false};

  std::vector<std::thread> hogs;
  for (int i = 0; i < 8; ++i) {
    hogs.emplace_back([&] {
      while (!stop.load(std::memory_order_relaxed)) {
        lock();
        hog_turns.fetch_add(1, std::memory_order_relaxed);
        unlock();
      }
    });
  }
  std::this_thread::sleep_for(50ms);

  auto deadline = std::chrono::steady_clock::now() + 200ms;
  const long hog_before = hog_turns.load();   // 窗口开头的快照
  long worst_us = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    auto t0 = std::chrono::steady_clock::now();
    lock();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0).count();
    if (us > worst_us) worst_us = us;
    victim_turns.fetch_add(1, std::memory_order_relaxed);
    unlock();
  }
  const long hog_during = hog_turns.load() - hog_before;

  stop.store(true);
  for (auto& t : hogs) t.join();
  std::printf("in 200 ms: 8 threads took the lock %ld times, the other %ld times\n",
              hog_during, victim_turns.load());
  std::printf("its longest single wait: %ld us\n", worst_us);
}
