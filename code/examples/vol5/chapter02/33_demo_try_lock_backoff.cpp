// demo_try_lock_backoff.cpp —— std::try_lock：试不上就把拿到的全放回去
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

std::mutex m1, m2;

int main() {
  std::thread holder([&] {
    std::lock_guard<std::mutex> lock(m1);
    std::printf("holder: m1 taken, keeping it for 300 ms\n");
    std::fflush(stdout);
    std::this_thread::sleep_for(300ms);
  });

  std::this_thread::sleep_for(50ms);

  // 参数次序是 m2、m1。m2 本来是空的，所以它会拿到 m2，再去试 m1。
  int index = std::try_lock(m2, m1);
  std::printf("main: try_lock(m2, m1) returned %d\n", index);

  // 失败以后 m2 应该已经被放回去了，咱们试着拿一下看看。
  bool got_m2 = m2.try_lock();
  std::printf("main: m2 is %s\n", got_m2 ? "free" : "still held");
  if (got_m2) m2.unlock();

  holder.join();
  return 0;
}
