// stop_flag.cpp —— 停止标志：release 置、acquire 读，标志旁边的数据跟着过桥
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

std::atomic<bool> stop_flag{false};
int batch_size = 8;              // 标志旁边共享的配置，普通变量

int config_after_stop = 0;

void worker() {
  while (!stop_flag.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  config_after_stop = batch_size;   // 看到停止之后读配置
}

int main() {
  std::thread w(worker);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  batch_size = 64;                  // 配置更新压在置标志之前
  stop_flag.store(true, std::memory_order_release);
  w.join();
  std::printf("worker 看到停止后读到的 batch_size=%d\n", config_after_stop);
}
