// wait_for_six_cases.cpp —— 带超时的等待：带谓词与不带谓词各三种情形，摆在一起跑
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

const char* name_of(std::cv_status s) {
  return s == std::cv_status::timeout ? "timeout" : "no_timeout";
}

int main() {
  {
    std::mutex m;
    std::condition_variable cv;
    bool ready = true;
    std::unique_lock<std::mutex> lk(m);
    auto t0 = Clock::now();
    bool ok = cv.wait_for(lk, 200ms, [&] { return ready; });
    std::printf("A pred 本来成立   -> %d, %.3f ms\n", ok, ms_since(t0));
  }
  {
    std::mutex m;
    std::condition_variable cv;
    bool ready = false;
    std::thread t([&] {
      std::this_thread::sleep_for(30ms);
      { std::lock_guard<std::mutex> g(m); ready = true; }
      cv.notify_one();
    });
    std::unique_lock<std::mutex> lk(m);
    auto t0 = Clock::now();
    bool ok = cv.wait_for(lk, 200ms, [&] { return ready; });
    double e = ms_since(t0);
    lk.unlock();
    t.join();
    std::printf("B 30 ms 被通知    -> %d, %.3f ms\n", ok, e);
  }
  {
    std::mutex m;
    std::condition_variable cv;
    bool ready = false;
    std::unique_lock<std::mutex> lk(m);
    auto t0 = Clock::now();
    bool ok = cv.wait_for(lk, 60ms, [&] { return ready; });
    std::printf("C 60 ms 到期      -> %d, %.3f ms\n", ok, ms_since(t0));
  }
  {
    std::mutex m;
    std::condition_variable cv;
    bool ready = false;
    std::thread t([&] {
      std::this_thread::sleep_for(30ms);
      { std::lock_guard<std::mutex> g(m); ready = true; }
      cv.notify_one();
    });
    std::unique_lock<std::mutex> lk(m);
    auto t0 = Clock::now();
    std::cv_status st = cv.wait_for(lk, 200ms);
    double e = ms_since(t0);
    lk.unlock();
    t.join();
    std::printf("D 无谓词，30 ms 通知 -> %s, %.3f ms\n", name_of(st), e);
  }
  {
    std::mutex m;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lk(m);
    auto t0 = Clock::now();
    std::cv_status st = cv.wait_for(lk, 60ms);
    std::printf("E 无谓词，没人通知   -> %s, %.3f ms\n", name_of(st), ms_since(t0));
  }
  {
    std::mutex m;
    std::condition_variable cv;
    bool ready = true;
    std::unique_lock<std::mutex> lk(m);
    auto t0 = Clock::now();
    bool ok = cv.wait_for(lk, -1ms, [&] { return ready; });
    std::printf("F 时限已过但 pred 成立 -> %d, %.3f ms\n", ok, ms_since(t0));
  }
  return 0;
}
