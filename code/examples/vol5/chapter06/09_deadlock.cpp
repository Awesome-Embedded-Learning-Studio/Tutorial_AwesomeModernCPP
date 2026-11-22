#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

std::mutex ma;
std::mutex mb;

void work_a() {
  std::lock_guard<std::mutex> la(ma);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  std::lock_guard<std::mutex> lb(mb);   // A 后 B 的次序
  std::printf("a got both\n");
}

void work_b() {
  std::lock_guard<std::mutex> lb(mb);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  std::lock_guard<std::mutex> la(ma);   // B 后 A 的次序
  std::printf("b got both\n");
}

int main() {
  std::thread a(work_a);
  std::thread b(work_b);
  a.join();
  b.join();
  return 0;
}
