// demo_wait_for_spins.cpp —— deferred 的 wait_for 立刻返回，不执行也不等待
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

double since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

const char* name_of(std::future_status s) {
  switch (s) {
    case std::future_status::deferred: return "deferred";
    case std::future_status::ready:    return "ready";
    default:                           return "timeout";
  }
}

int work() {
  std::this_thread::sleep_for(50ms);
  return 7;
}

int main() {
  std::future<int> f = std::async(std::launch::deferred, work);
  for (int i = 1; i <= 3; ++i) {
    auto t0 = Clock::now();
    std::future_status st = f.wait_for(100ms);
    std::printf("第 %d 次 wait_for(100ms) -> %-8s 实际用时 %.3f ms\n",
                i, name_of(st), since(t0));
  }
  std::printf("改用 get()，任务这才执行 -> %d\n", f.get());
  return 0;
}
