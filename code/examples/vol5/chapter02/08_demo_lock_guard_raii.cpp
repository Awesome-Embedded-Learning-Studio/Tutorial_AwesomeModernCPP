// demo_lock_guard_raii.cpp —— 换成 lock_guard：异常路上就把锁放了
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <thread>

std::mutex gate;

void work() {
  std::lock_guard<std::mutex> lock(gate);
  throw std::runtime_error("boom");
}

int main() {
  std::thread t([] {
    try {
      work();
    } catch (const std::exception& e) {
      std::printf("[worker] caught %s\n", e.what());
    }
  });
  t.join();

  std::printf("[main] asking for the same lock\n");
  gate.lock();   // 构造时拿锁、析构时放锁：锁在异常离开作用域的路上就放掉了
  std::printf("[main] got it\n");
  gate.unlock();
  return 0;
}
