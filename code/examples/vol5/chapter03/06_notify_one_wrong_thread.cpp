// notify_one_wrong_thread.cpp —— 一把 cv 睡两拨人：notify_one 叫醒的是早睡的 closer，taker 错过这趟
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

std::mutex m;
std::condition_variable cv;
bool stop = false;
bool data_ready = false;
std::atomic<int> parked{0};
std::atomic<bool> taker_got{false};

int main() {
  std::thread closer([] {
    std::unique_lock<std::mutex> lk(m);
    ++parked;
    cv.wait(lk, [] { return stop; });
    std::printf("closer: 收到 stop\n");
  });
  while (parked.load() < 1) std::this_thread::sleep_for(1ms);
  std::this_thread::sleep_for(20ms);

  std::thread taker([] {
    std::unique_lock<std::mutex> lk(m);
    ++parked;
    cv.wait(lk, [] { return data_ready; });
    taker_got = true;
    std::printf("taker: 拿到数据\n");
  });
  while (parked.load() < 2) std::this_thread::sleep_for(1ms);
  std::this_thread::sleep_for(20ms);

  {
    std::lock_guard<std::mutex> g(m);
    data_ready = true;
  }
  cv.notify_one();

  std::this_thread::sleep_for(200ms);
  std::printf("main: 200 ms 过去，taker %s\n",
              taker_got.load() ? "已经拿到数据" : "还没醒过来");

  {
    std::lock_guard<std::mutex> g(m);
    stop = true;
  }
  cv.notify_all();
  closer.join();
  taker.join();
  std::printf("main: 收工\n");
  return 0;
}
