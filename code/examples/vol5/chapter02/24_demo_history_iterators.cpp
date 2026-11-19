// demo_history_iterators.cpp —— 迭代器交出去的是位置：单线程也踩得中
#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

class History {
 public:
  void add(int v) {
    std::lock_guard<std::mutex> lock(m_);
    items_.push_back(v);
  }
  void set(std::size_t i, int v) {
    std::lock_guard<std::mutex> lock(m_);
    items_[i] = v;
  }
  std::vector<int>::iterator begin() {
    std::lock_guard<std::mutex> lock(m_);
    return items_.begin();
  }
  std::vector<int>::iterator end() {
    std::lock_guard<std::mutex> lock(m_);
    return items_.end();
  }

 private:
  mutable std::mutex m_;
  std::vector<int> items_;
};

int main() {
  History history;
  history.add(1);
  history.add(2);
  auto it = history.begin();   // 锁在 begin() 返回时放了
  history.add(3);              // 容量不够，重新分配，迭代器失效
  int x = *it;                 // 读的是已经释放的内存（ASan 下报 heap-use-after-free）
  std::printf("dangling iterator read: %d\n", x);

  History raced;
  raced.add(10);
  raced.add(20);
  auto rit = raced.begin();    // 锁在 begin() 返回时放了
  std::atomic<bool> done{false};
  std::thread writer([&] {
    for (int i = 1; i <= 100000; ++i) raced.set(0, i);
    done.store(true);
  });
  long long sum = 0;
  while (!done.load()) sum += *rit;   // 锁外的读（TSan 下报 data race）
  writer.join();
  std::printf("out-of-lock reads summed to %lld\n", sum);
  return 0;
}
