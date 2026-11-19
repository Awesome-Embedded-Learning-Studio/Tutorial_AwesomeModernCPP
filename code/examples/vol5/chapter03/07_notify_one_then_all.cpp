// notify_one_then_all.cpp —— 三条线程等同一个 go：notify_one 醒 1 条，notify_all 全放行
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

int main() {
  std::mutex m;
  std::condition_variable cv;
  bool go = false;
  std::atomic<int> started{0};
  std::atomic<int> woken{0};
  std::vector<std::thread> ws;
  for (int i = 0; i < 3; ++i) {
    ws.emplace_back([&, i] {
      std::unique_lock<std::mutex> lk(m);
      ++started;
      cv.wait(lk, [&] { return go; });
      ++woken;
      std::printf("worker %d: 出发\n", i);
    });
  }
  while (started.load() < 3) std::this_thread::sleep_for(1ms);
  std::this_thread::sleep_for(20ms);
  {
    std::lock_guard<std::mutex> g(m);
    go = true;
  }
  cv.notify_one();
  std::this_thread::sleep_for(100ms);
  std::printf("main: notify_one 之后，醒来的有 %d 条\n", woken.load());
  cv.notify_all();
  for (auto& t : ws) t.join();
  std::printf("main: notify_all 之后，醒来的有 %d 条\n", woken.load());
  return 0;
}
