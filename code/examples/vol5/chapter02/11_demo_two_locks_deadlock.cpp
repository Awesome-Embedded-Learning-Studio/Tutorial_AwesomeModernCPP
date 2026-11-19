// demo_two_locks_deadlock.cpp —— 各拿一把再嵌套要对方那把：死锁重现
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

struct Item {
  std::mutex m;
  long long value = 1000000;
};

void move(Item& from, Item& to, long long amount) {
  std::lock_guard<std::mutex> a(from.m);
  std::printf("holding one side\n");
  std::this_thread::sleep_for(50ms);
  std::lock_guard<std::mutex> b(to.m);
  from.value -= amount;
  to.value += amount;
}

int main() {
  Item x, y;
  std::thread t1([&] { move(x, y, 1); });
  std::thread t2([&] { move(y, x, 1); });
  t1.join();
  t2.join();
  std::printf("sum = %lld\n", x.value + y.value);
  return 0;
}
