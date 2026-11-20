// demo_three_policies.cpp —— 同一任务三种策略各跑一遍，看线程号和时刻
#include <chrono>
#include <cstdio>
#include <functional>
#include <future>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

std::size_t id_of() {
  return std::hash<std::thread::id>{}(std::this_thread::get_id());
}

double since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

void probe(const char* tag, Clock::time_point t0) {
  std::this_thread::sleep_for(50ms);   // 任务本体算作 50 ms 的活
  std::printf("  %s 在线程 %zu 上执行，距 async 返回 %.1f ms\n",
              tag, id_of(), since(t0));
}

void case_of(const char* tag, std::launch policy) {
  std::printf("[%s] main 线程是 %zu\n", tag, id_of());
  auto t0 = Clock::now();
  std::future<void> f = std::async(policy, probe, tag, t0);
  std::this_thread::sleep_for(100ms);  // 给后台任务留出 100 ms 的窗口
  std::printf("[%s] main 睡了 100 ms，现在调 get\n", tag);
  f.get();
  std::printf("[%s] get 返回，全程 %.1f ms\n\n", tag, since(t0));
}

int main() {
  case_of("async   ", std::launch::async);
  case_of("deferred", std::launch::deferred);
  case_of("default ", std::launch::async | std::launch::deferred);
  return 0;
}
