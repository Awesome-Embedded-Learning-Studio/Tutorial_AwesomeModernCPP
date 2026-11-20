// lost_wakeup_fixed.cpp —— 丢失唤醒的修法：整个程序只改一行，裸 wait 换成带谓词的 wait
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
    cv.wait(lk, [] { return ready; });    // 谓词在 wait 之前就要算一遍
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
