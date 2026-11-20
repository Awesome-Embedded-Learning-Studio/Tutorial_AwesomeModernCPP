// demo_manual_lock_ub.cpp —— 手动 lock/unlock：异常一抛，锁留在原地
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <thread>

std::mutex gate;

void work() {
  gate.lock();
  throw std::runtime_error("boom");
  gate.unlock();
}

int main() {
  std::thread t([] {
    try {
      work();
    } catch (const std::exception& e) {
      std::printf("[worker] caught %s, the lock is still held\n", e.what());
    }
  });
  t.join();

  std::printf("[main] asking for the same lock\n");
  gate.lock();   // 工作线程退出时仍持有锁：未定义行为，这里是一直等
  std::printf("[main] got it\n");
  gate.unlock();
  return 0;
}
