// stop_bool.cpp —— 普通布尔做停止标志：-O0 正常收场，-O2 工作线程出不来
// 演示用：stop 上的跨线程读写是数据竞争，标准已不作要求，别当写法抄
#include <chrono>
#include <cstdio>
#include <thread>

bool stop = false;   // 故意不用 std::atomic<bool>，本节只做演示

int main() {
  long long spins = 0;
  std::thread t([&] {
    while (!stop) ++spins;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  stop = true;
  t.join();
  std::printf("main set stop, thread saw it, spins=%lld\n", spins);
}
