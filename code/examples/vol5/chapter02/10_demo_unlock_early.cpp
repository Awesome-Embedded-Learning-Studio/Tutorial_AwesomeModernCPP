// demo_unlock_early.cpp —— 拷完快照就放锁：主线程少等 45 毫秒
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

using clock_type = std::chrono::steady_clock;
using namespace std::chrono_literals;

long long run(bool unlock_after_copy) {
  std::mutex m;
  std::vector<int> shared(100000, 1);
  long long waited = -1;

  std::thread holder([&] {
    std::unique_lock<std::mutex> lock(m);
    auto snapshot = shared;
    if (unlock_after_copy) lock.unlock();
    std::this_thread::sleep_for(50ms);   // 顶替耗时的活
    long long sink = 0;
    for (int v : snapshot) sink += v;
    if (sink < 0) std::puts("unreachable");
  });

  std::this_thread::sleep_for(5ms);
  auto t0 = clock_type::now();
  {
    std::lock_guard<std::mutex> lock(m);
    waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                 clock_type::now() - t0).count();
  }
  holder.join();
  return waited;
}

int main() {
  std::printf("lock held through the work: waited %lld ms\n", run(false));
  std::printf("unlock after the copy:     waited %lld ms\n", run(true));
  return 0;
}
