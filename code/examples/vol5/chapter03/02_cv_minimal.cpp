// cv_minimal.cpp —— 能跑的最小版本：worker 改完条件叫一声，waiter 睡到条件成立
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace std::chrono_literals;

std::mutex m;
std::condition_variable cv;
bool done = false;   // 由 m 保护

void worker() {
  // 干活
  {
    std::lock_guard<std::mutex> g(m);
    done = true;
  }
  cv.notify_one();
}

void waiter() {
  std::unique_lock<std::mutex> lk(m);
  cv.wait(lk, [] { return done; });
  // 到这里 done 一定是 true，锁还在手里
}

int main() {
  std::thread t(waiter);
  std::this_thread::sleep_for(50ms);   // 让 waiter 先查条件、先睡下去
  worker();                            // 改条件，再通知
  t.join();
  std::printf("waiter 回来了，done = %d\n", done ? 1 : 0);
  return 0;
}
