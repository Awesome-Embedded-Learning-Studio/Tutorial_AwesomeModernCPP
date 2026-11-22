// demo_boundary.cpp —— 检查和动手分成两次原子操作，货照样卖超
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace std::chrono_literals;

std::atomic<int> stock{1};
std::atomic<bool> a_checked{false};

void stage_naive() {
  stock.store(1);
  std::thread a([&] {
    if (stock.load() > 0) {                 // 检查：一次原子读
      a_checked.store(true);
      std::this_thread::sleep_for(100ms);   // 窗口
      stock.store(stock.load() - 1);        // 动手：读一次、写一次，各自原子
      std::printf("[naive  ] buyer A 动手，结余 %d\n", stock.load());
    }
  });
  while (!a_checked.load()) {}              // 手动轮询等 A 查完
  std::this_thread::sleep_for(10ms);
  if (stock.load() > 0) {                   // buyer B：也看到了 1
    stock.store(stock.load() - 1);
    std::printf("[naive  ] buyer B 动手，结余 %d\n", stock.load());
  }
  a.join();
}

bool try_buy_cas() {                        // 判断和扣减压进一次读改写
  int old = stock.load();
  while (old > 0) {
    if (stock.compare_exchange_weak(old, old - 1)) return true;
  }
  return false;
}

void stage_cas() {
  stock.store(1);
  a_checked.store(false);
  std::thread a([&] {
    int old = stock.load();
    if (old > 0) {
      a_checked.store(true);
      std::this_thread::sleep_for(100ms);
      if (try_buy_cas())
        std::printf("[cas    ] buyer A 动手，结余 %d\n", stock.load());
      else
        std::printf("[cas    ] buyer A 重试看见 0，放弃\n");
    }
  });
  while (!a_checked.load()) {}
  std::this_thread::sleep_for(10ms);
  if (try_buy_cas())
    std::printf("[cas    ] buyer B 动手，结余 %d\n", stock.load());
  a.join();
}

int main() {
  stage_naive();
  stage_cas();
  return 0;
}
