// stop_wake.cpp —— stop_token 进等待：第一幕空手走，第二幕货照交
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

std::mutex m;
std::condition_variable_any cv;
std::queue<int> q;
std::atomic<int> parked{0};

double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
  auto t0 = Clock::now();

  { // 第一幕：队列空着，叫醒它的是停止请求
    std::jthread consumer([&](std::stop_token st) {
      std::unique_lock<std::mutex> lk(m);
      ++parked;
      bool got = cv.wait(lk, st, [] { return !q.empty(); });
      std::printf("consumer: %5.1f ms 离开等待，返回值 %d（队列是空的）\n",
                  ms_since(t0), got ? 1 : 0);
    });
    while (parked.load() < 1) std::this_thread::sleep_for(1ms);
    std::this_thread::sleep_for(60ms);
    std::printf("main    : %5.1f ms 请求停止\n", ms_since(t0));
    std::fflush(stdout);
    std::this_thread::sleep_for(2ms);
    consumer.request_stop();
  }   // jthread 析构：停止已经请求过，这里只剩 join

  while (!q.empty()) q.pop();
  parked = 0;
  auto t1 = Clock::now();

  { // 第二幕：货到了，通知故意不发，停止请求负责叫醒
    std::jthread consumer([&](std::stop_token st) {
      std::unique_lock<std::mutex> lk(m);
      ++parked;
      bool got = cv.wait(lk, st, [] { return !q.empty(); });
      if (got) {
        int v = q.front();
        q.pop();
        std::printf("consumer: %5.1f ms 拿到 %d，此刻 stop_requested = %d\n",
                    ms_since(t1), v, st.stop_requested() ? 1 : 0);
      } else {
        std::printf("consumer: %5.1f ms 空手离开，返回值 false\n", ms_since(t1));
      }
    });
    while (parked.load() < 1) std::this_thread::sleep_for(1ms);
    { std::lock_guard<std::mutex> g(m); q.push(42); }  // 故意不 notify
    std::this_thread::sleep_for(60ms);
    std::printf("main    : %5.1f ms 请求停止\n", ms_since(t1));
    std::fflush(stdout);
    std::this_thread::sleep_for(2ms);
    consumer.request_stop();
  }
  return 0;
}
