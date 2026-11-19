// demo_ops.cpp —— load/store/exchange/fetch_add 与运算符形式
#include <atomic>
#include <cstdio>

int main() {
  std::atomic<int> counter{0};

  counter.store(10);
  std::printf("store(10) 之后 load      : %d\n", counter.load());

  int old = counter.exchange(3);
  std::printf("exchange(3) 交回旧值     : %d，现在 %d\n", old, counter.load());

  old = counter.fetch_add(5);
  std::printf("fetch_add(5) 交回旧值    : %d，现在 %d\n", old, counter.load());

  old = counter++;                  // 后置自增：返回加之前的值
  std::printf("counter++ 交回旧值       : %d，现在 %d\n", old, counter.load());

  int now = ++counter;              // 前置自增：返回加完之后的值
  std::printf("++counter 交回新值       : %d，现在 %d\n", now, counter.load());

  counter += 4;                     // 运算符形式，等价 fetch_add(4)
  std::printf("+= 4 之后                : %d\n", counter.load());
  return 0;
}
