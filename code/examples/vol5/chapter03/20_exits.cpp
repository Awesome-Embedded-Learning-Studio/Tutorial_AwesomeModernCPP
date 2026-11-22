// exits.cpp —— 同一处等待，四种结束方式，各留一个可观察的现场
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

struct Bay {
  std::mutex m;
  std::condition_variable_any cv;   // 通用版：C 一幕要接停止请求
  std::queue<int> q;
  bool closed = false;
  bool failed = false;
  std::atomic<int> parked{0};
};

double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
  { // A 关闭：没人再来放货，收盘的一方把 closed 立起来
    Bay bay;
    auto t0 = Clock::now();
    std::thread waiter([&] {
      std::unique_lock<std::mutex> lk(bay.m);
      ++bay.parked;
      bay.cv.wait(lk, [&] { return !bay.q.empty() || bay.closed; });
      std::printf("A 关闭 : %5.1f ms 返回，队列空着，谓词翻正靠的是 closed\n",
                  ms_since(t0));
    });
    while (bay.parked.load() < 1) std::this_thread::sleep_for(1ms);
    std::this_thread::sleep_for(60ms);
    { std::lock_guard<std::mutex> g(bay.m); bay.closed = true; }
    bay.cv.notify_all();
    waiter.join();
  }

  { // B 超时：时限 120 ms，谁也没来
    Bay bay;
    auto t0 = Clock::now();
    std::thread waiter([&] {
      std::unique_lock<std::mutex> lk(bay.m);
      ++bay.parked;
      bool got = bay.cv.wait_for(lk, 120ms,
                                 [&] { return !bay.q.empty() || bay.closed; });
      std::printf("B 超时 : %5.1f ms 返回，返回值 %d，时限内没等到货\n",
                  ms_since(t0), got ? 1 : 0);
    });
    while (bay.parked.load() < 1) std::this_thread::sleep_for(1ms);
    waiter.join();
  }

  { // C 停止：停止请求在 60 ms 到，等待当场结束
    Bay bay;
    std::stop_source source;
    auto token = source.get_token();
    auto t0 = Clock::now();
    std::thread waiter([&] {
      std::unique_lock<std::mutex> lk(bay.m);
      ++bay.parked;
      bool got = bay.cv.wait(lk, token, [&] { return !bay.q.empty(); });
      std::printf("C 停止 : %5.1f ms 返回，返回值 %d，谓词没成立\n",
                  ms_since(t0), got ? 1 : 0);
    });
    while (bay.parked.load() < 1) std::this_thread::sleep_for(1ms);
    std::this_thread::sleep_for(60ms);
    std::printf("main   : %5.1f ms 发出停止请求\n", ms_since(t0));
    std::fflush(stdout);
    std::this_thread::sleep_for(2ms);
    source.request_stop();
    waiter.join();
  }

  { // D 失败：上游干砸了，把 failed 记下来再散场
    Bay bay;
    auto t0 = Clock::now();
    std::thread waiter([&] {
      std::unique_lock<std::mutex> lk(bay.m);
      ++bay.parked;
      bay.cv.wait(lk, [&] { return !bay.q.empty() || bay.failed; });
      std::printf("D 失败 : %5.1f ms 返回，货没等到，谓词翻正靠的是 failed\n",
                  ms_since(t0));
    });
    while (bay.parked.load() < 1) std::this_thread::sleep_for(1ms);
    std::this_thread::sleep_for(70ms);
    { std::lock_guard<std::mutex> g(bay.m); bay.failed = true; }
    bay.cv.notify_all();
    waiter.join();
  }
  return 0;
}
