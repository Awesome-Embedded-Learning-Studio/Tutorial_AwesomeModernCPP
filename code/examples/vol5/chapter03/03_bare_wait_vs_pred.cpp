// bare_wait_vs_pred.cpp —— 虚假唤醒的替代演示：条件没变就发的通知，两种 wait 各接一次
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

std::mutex m;
std::condition_variable cv;
bool data_ready = false;

int main() {
  {
    std::thread waiter([] {
      std::unique_lock<std::mutex> lk(m);
      cv.wait(lk);  // 裸 wait，醒来不查条件
      std::printf("[bare] 醒来时 data_ready = %d\n", data_ready ? 1 : 0);
    });
    std::this_thread::sleep_for(50ms);
    cv.notify_one();  // 什么也没改，只发一次通知
    waiter.join();
  }

  data_ready = false;
  {
    std::thread waiter([] {
      std::unique_lock<std::mutex> lk(m);
      cv.wait(lk, [] { return data_ready; });
      std::printf("[pred] 醒来时 data_ready = %d\n", data_ready ? 1 : 0);
    });
    std::this_thread::sleep_for(50ms);
    cv.notify_one();
    std::this_thread::sleep_for(50ms);
    std::printf("[main] 第一次通知之后，等待线程还睡着\n");
    {
      std::lock_guard<std::mutex> g(m);
      data_ready = true;
    }
    cv.notify_one();
    waiter.join();
  }
  return 0;
}
