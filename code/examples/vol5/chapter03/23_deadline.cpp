// deadline.cpp —— 超时和停止拼进同一次调用
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <stop_token>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

std::mutex m;
std::condition_variable_any cv;
bool done = false;
std::atomic<int> parked{0};

double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
  auto t0 = Clock::now();
  std::stop_source source;
  auto token = source.get_token();
  auto deadline = Clock::now() + 300ms;   // 时限从调用方来

  {
    std::jthread waiter([&] {
      std::unique_lock<std::mutex> lk(m);
      ++parked;
      bool ok = cv.wait_until(lk, token, deadline, [&] { return done; });
      if (ok) {
        std::printf("waiter: %5.1f ms 返回，done 成了\n", ms_since(t0));
      } else if (token.stop_requested()) {
        std::printf("waiter: %5.1f ms 返回，返回值 false，叫醒它的是停止请求\n",
                    ms_since(t0));
      } else {
        std::printf("waiter: %5.1f ms 返回，返回值 false，时限到头\n", ms_since(t0));
      }
    });
    while (parked.load() < 1) std::this_thread::sleep_for(1ms);
    std::this_thread::sleep_for(60ms);
    std::printf("main  : %5.1f ms 请求停止（时限其实还没到）\n", ms_since(t0));
    std::fflush(stdout);
    std::this_thread::sleep_for(2ms);
    source.request_stop();
  }   // jthread 析构：停止已请求过，这里只剩 join
  return 0;
}
