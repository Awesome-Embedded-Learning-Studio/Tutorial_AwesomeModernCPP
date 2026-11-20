// demo_deferred_thread_local.cpp —— deferred 在调用 get 的线程里跑，thread_local 踩空
#include <cstdio>
#include <functional>
#include <future>
#include <thread>

thread_local int counter = 0;   // 每条线程各有一份

std::size_t id_of() {
  return std::hash<std::thread::id>{}(std::this_thread::get_id());
}

int bump() {
  counter += 1;
  std::printf("  bump 在线程 %zu 上，counter 算到 %d\n", id_of(), counter);
  return counter;
}

int main() {
  std::printf("main 线程是 %zu，counter 加到 3\n", id_of());
  counter = 3;
  std::future<int> fa = std::async(std::launch::async, bump);
  std::future<int> fd = std::async(std::launch::deferred, bump);
  int a = fa.get();
  int d = fd.get();
  std::printf("async 的答案 %d，deferred 的答案 %d\n", a, d);
  std::printf("main 手里的 counter 是 %d\n", counter);
  return 0;
}
