// demo_shelf_try_take.cpp —— 判断与扣减进同一个临界区：只卖出一件
#include <cstdio>
#include <mutex>
#include <thread>

struct Shelf {
  std::mutex m;
  int stock = 1;

  bool try_take() {
    std::lock_guard<std::mutex> lock(m);
    if (stock == 0) return false;
    --stock;
    return true;
  }
};

Shelf shelf;

void buy(const char* who) {
  if (shelf.try_take()) {
    std::printf("%s: takes the last one\n", who);
  } else {
    std::printf("%s: finds none left\n", who);
  }
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
