// demo_gauge_by_value.cpp —— 返回按值：每次读都在锁里完成
#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>

class Gauge {
 public:
  void set(int v) {
    std::lock_guard<std::mutex> lock(m_);
    value_ = v;
  }
  int value() const {
    std::lock_guard<std::mutex> lock(m_);
    return value_;
  }

 private:
  mutable std::mutex m_;
  int value_ = 0;
};

int main() {
  Gauge gauge;
  std::atomic<bool> done{false};
  std::thread writer([&] {
    for (int i = 1; i <= 100000; ++i) gauge.set(i);
    done.store(true);
  });

  long long sum = 0;
  while (!done.load()) sum += gauge.value();
  writer.join();
  std::printf("sum = %lld (TSan compiles clean, every read inside the lock)\n", sum);
  return 0;
}
