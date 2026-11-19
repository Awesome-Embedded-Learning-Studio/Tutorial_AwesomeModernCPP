// demo_shelf_check_then_act.cpp —— 检查与动手分开：一件货卖给了两位买家
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

struct Shelf {
  std::mutex m;
  int stock = 1;

  bool has_one() {
    std::lock_guard<std::mutex> lock(m);
    return stock > 0;
  }
  void take_one() {
    std::lock_guard<std::mutex> lock(m);
    --stock;
  }
};

Shelf shelf;

void buy(const char* who) {
  if (!shelf.has_one()) {
    std::printf("%s: sold out\n", who);
    return;
  }
  std::printf("%s: sees stock = 1\n", who);
  std::this_thread::sleep_for(50ms);   // 把检查与动手之间的窗口放大
  shelf.take_one();
  std::printf("%s: takes one\n", who);
}

int main() {
  const int kRounds = 3;
  int total_sold = 0;
  for (int r = 1; r <= kRounds; ++r) {
    shelf.stock = 1;
    std::thread a(buy, "buyer A");
    std::thread b(buy, "buyer B");
    a.join();
    b.join();
    int sold = 1 - shelf.stock;
    total_sold += sold;
    std::printf("round %d: stock left %d, sold %d\n", r, shelf.stock, sold);
  }
  std::printf("%d rounds, %d sold\n", kRounds, total_sold);
  return 0;
}
