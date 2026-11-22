// demo_peak_inside.cpp —— 互斥的证据：锁里至多一条线程
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

std::mutex m;
int inside = 0;
int peak = 0;

void visit() {
  std::lock_guard<std::mutex> lock(m);
  ++inside;
  if (inside > peak) peak = inside;
  std::this_thread::sleep_for(1ms);
  --inside;
}

int main() {
  std::thread ts[4];
  for (auto& t : ts) t = std::thread(visit);
  for (auto& t : ts) t.join();
  std::printf("peak inside = %d, left inside = %d\n", peak, inside);
  return 0;
}
