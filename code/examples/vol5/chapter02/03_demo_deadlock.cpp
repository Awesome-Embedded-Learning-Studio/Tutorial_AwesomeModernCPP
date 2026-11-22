// demo_deadlock.cpp —— 各持一把锁，等对方松手：跑起来会停在两行
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

std::mutex m1, m2;

void route_one() {
  std::lock_guard<std::mutex> a(m1);
  std::puts("[one] holds m1");
  std::this_thread::sleep_for(50ms);
  std::lock_guard<std::mutex> b(m2);
  std::puts("[one] got m2 as well");
}

void route_two() {
  std::lock_guard<std::mutex> b(m2);
  std::puts("[two] holds m2");
  std::this_thread::sleep_for(50ms);
  std::lock_guard<std::mutex> a(m1);
  std::puts("[two] got m1 as well");
}

int main() {
  std::thread t1(route_one);
  std::thread t2(route_two);
  t1.join();
  t2.join();
  std::puts("both finished");
}
