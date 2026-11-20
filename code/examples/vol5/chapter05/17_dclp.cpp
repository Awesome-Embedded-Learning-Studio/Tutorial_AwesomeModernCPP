// dclp.cpp —— 双重检查锁定：外层 acquire，锁内 relaxed，发布 release
#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>

struct Config { int id; int value; };
Config* raw_instance = nullptr;              // 真正的对象
std::atomic<Config*> published{nullptr};     // 发布口
std::mutex init_mutex;

__attribute__((noinline)) Config* instance() {
  Config* p = published.load(std::memory_order_acquire);        // 外层检查，acquire
  if (p == nullptr) {
    std::lock_guard<std::mutex> lock(init_mutex);
    p = published.load(std::memory_order_relaxed);              // 锁内复查，已持锁，relaxed 就够
    if (p == nullptr) {
      raw_instance = new Config{1, 42};                         // 构造全部完成才发布
      published.store(raw_instance, std::memory_order_release); // 桥从这里搭
      p = raw_instance;      // 发布后把指针带回来；第一版漏了这行，抢到锁的线程拿着 nullptr 回去
    }
  }
  return p;
}

const Config* seen[8] = {};

int main() {
  std::thread ts[8];
  for (int i = 0; i < 8; ++i) {
    ts[i] = std::thread([i] {                  // seen 是全局数组，lambda 里直接可见，不用捕获
      for (int k = 0; k < 100000; ++k) {
        const Config* p = instance();
        if (p->id != 1 || p->value != 42) {
          std::printf("线程 %d 读到不一致的对象\n", i);
        }
        seen[i] = p;
      }
    });
  }
  for (auto& t : ts) t.join();
  bool all_same = true;
  for (int i = 1; i < 8; ++i) if (seen[i] != seen[0]) all_same = false;
  std::printf("8 条线程各取 10 万次：指针一致=%d，id=%d value=%d\n",
              all_same ? 1 : 0, seen[0]->id, seen[0]->value);
}
