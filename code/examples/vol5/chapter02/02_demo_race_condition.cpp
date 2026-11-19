// demo_race_condition.cpp —— 没有 data race，照样丢计数：原子上的读改写
#include <atomic>
#include <cstdio>
#include <thread>

std::atomic<int> counter{0};

void work() {
  for (int i = 0; i < 100000; ++i) {
    counter = counter.load() + 1;
  }
}

int main() {
  std::thread a(work);
  std::thread b(work);
  a.join();
  b.join();
  std::printf("%d\n", counter.load());
}
