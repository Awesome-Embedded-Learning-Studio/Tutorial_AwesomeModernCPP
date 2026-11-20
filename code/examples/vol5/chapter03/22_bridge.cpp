// bridge.cpp —— 手头只有 condition_variable 时，用 stop_callback 搭桥
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
std::condition_variable cv;
bool closed = false;
std::atomic<int> parked{0};

double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
  auto t0 = Clock::now();
  std::stop_source source;
  auto token = source.get_token();

  std::thread waiter([&] {
    // 桥：注册赶在拿锁之前，两头都罩得住
    // 注册时已立：回调当场在注册线程执行，此时 m 还空着，不会自己卡自己
    // 等待中到达：回调跑在请求方线程，拿同一把锁挡缝
    std::stop_callback cb(token, [&] {
      { std::lock_guard<std::mutex> g(m); }   // 拿同一把锁，挡住检查与入睡之间的缝
      cv.notify_all();
    });
    std::unique_lock<std::mutex> lk(m);
    ++parked;
    cv.wait(lk, [&] { return closed || token.stop_requested(); });
    std::printf("waiter: %5.1f ms 离开等待，closed = %d\n",
                ms_since(t0), closed ? 1 : 0);
  });

  while (parked.load() < 1) std::this_thread::sleep_for(1ms);
  std::this_thread::sleep_for(60ms);
  std::printf("main  : %5.1f ms 请求停止\n", ms_since(t0));
  std::fflush(stdout);
  std::this_thread::sleep_for(2ms);
  source.request_stop();
  waiter.join();
  return 0;
}
