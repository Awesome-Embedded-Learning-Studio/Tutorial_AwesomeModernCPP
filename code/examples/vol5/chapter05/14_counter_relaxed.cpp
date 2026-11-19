// counter_relaxed.cpp —— relaxed 只保原子性：四条线程各加 100 万次
#include <atomic>
#include <cstdio>
#include <thread>

std::atomic<long long> served{0};

void work(int rounds) {
  for (int i = 0; i < rounds; ++i) {
    served.fetch_add(1, std::memory_order_relaxed);   // 只保原子性，不搭桥
  }
}

int main() {
  std::thread ts[4];
  for (int i = 0; i < 4; ++i) ts[i] = std::thread(work, 1000000);
  for (auto& t : ts) t.join();
  std::printf("4 条线程各加 100 万次，relaxed 读数 %lld\n", served.load(std::memory_order_relaxed));
}
