// lost_wakeup.cpp —— 丢失唤醒现场：通知发在等待之前，worker 再也等不到叫醒
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

std::mutex m;
std::condition_variable cv;
bool ready = false;

int main() {
  std::thread worker([] {
    std::this_thread::sleep_for(200ms);   // 起来得比通知晚
    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk);                          // 裸 wait，醒来不查条件
    std::printf("worker: 醒来了，ready = %d\n", ready ? 1 : 0);
  });

  std::this_thread::sleep_for(50ms);
  {
    std::lock_guard<std::mutex> g(m);
    ready = true;
  }
  cv.notify_one();                        // 这一刻 cv 上还没有人
  std::printf("main: 通知发完了\n");
  std::fflush(stdout);

  worker.join();
  std::printf("main: 走到底了\n");
  return 0;
}
