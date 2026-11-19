// pubsub.cpp —— 发布订阅：relaxed 计数只报进度，acquire/release 标志承担可见性
#include <atomic>
#include <cstdio>
#include <thread>

std::atomic<int> finished{0};    // 进度计数，relaxed
std::atomic<bool> done{false};   // 完成标志，release/acquire
int goods[3] = {0, 0, 0};        // 生产的三份货，普通变量

void producer() {
  for (int i = 0; i < 3; ++i) {
    goods[i] = 100 + i;
    finished.fetch_add(1, std::memory_order_relaxed);   // 只报进度
  }
  done.store(true, std::memory_order_release);          // 桥在这里搭
}

int seen_count = -1;
int seen_goods[3] = {0, 0, 0};

void consumer() {
  while (!done.load(std::memory_order_acquire)) {}      // 读到 release 写的值，桥接通
  seen_count = finished.load(std::memory_order_relaxed); // 桥接通后，读进度连 relaxed 都够
  for (int i = 0; i < 3; ++i) seen_goods[i] = goods[i];
}

int main() {
  std::thread p(producer);
  std::thread c(consumer);
  p.join();
  c.join();
  std::printf("消费者看到的计数=%d，货物=%d,%d,%d\n",
              seen_count, seen_goods[0], seen_goods[1], seen_goods[2]);
}
