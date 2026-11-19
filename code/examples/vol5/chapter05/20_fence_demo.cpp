// fence_demo.cpp —— fence-fence 同步：两端各挂一条 fence，排序约束从原子操作挪到 fence 上
#include <atomic>
#include <cstdio>
#include <thread>

int payload = 0;                 // 普通变量，可见性靠 fence 与 flag 共同垫起来
std::atomic<int> flag{0};

__attribute__((noinline)) void publish() {
  payload = 42;
  std::atomic_thread_fence(std::memory_order_release);   // 独立 fence 承担排序
  flag.store(1, std::memory_order_relaxed);              // relaxed store 就够
}

int received = 0;

__attribute__((noinline)) void subscribe() {
  while (flag.load(std::memory_order_relaxed) != 1) {}
  std::atomic_thread_fence(std::memory_order_acquire);
  received = payload;                                     // 依据标准语义，此处读到 42
}

int main() {
  std::thread p(publish);
  std::thread c(subscribe);
  p.join();
  c.join();
  std::printf("消费者读到的 payload=%d\n", received);
}
