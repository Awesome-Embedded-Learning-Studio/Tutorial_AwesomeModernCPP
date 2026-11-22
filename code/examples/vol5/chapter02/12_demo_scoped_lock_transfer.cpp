// demo_scoped_lock_transfer.cpp —— scoped_lock 一趟拿两把：死锁不再来
#include <cstdio>
#include <mutex>
#include <thread>

struct Item {
  std::mutex m;
  long long value = 1000000;
};

void move(Item& from, Item& to, long long amount) {
  std::scoped_lock lock(from.m, to.m);
  from.value -= amount;
  to.value += amount;
}

int main() {
  Item x, y;
  const int kIters = 200000;
  std::thread t1([&] {
    for (int i = 0; i < kIters; ++i) move(x, y, 1);
  });
  std::thread t2([&] {
    for (int i = 0; i < kIters; ++i) move(y, x, 2);
  });
  t1.join();
  t2.join();
  std::printf("sum = %lld, x = %lld, y = %lld\n",
              x.value + y.value, x.value, y.value);
  return 0;
}
